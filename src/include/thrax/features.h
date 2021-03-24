// Copyright 2005-2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Represents features, categories (bundles of features), and feature vectors
// (particular instantiations of categories) as FSTs. While one could do all of
// this without this module, it makes it easier since once one has defined
// features, one no longer has to worry about specifying the order of the
// feature values or of specifying all feature values.
//
// To take an example, one can define a set of features as follows:
//
// case = Feature['case', 'nom', 'acc', 'gen', 'dat'];
// number = Feature['number', 'sg', 'pl'];
// gender = Feature['gender', 'mas', 'fem', 'neu'];
//
// Then a category 'noun' as being a collection of these features:
//
// noun = Category[case, number, gender];
//
// Then a particular noun form could be defined as
//
// neu_nom = FeatureVector[noun, 'gender=neu', 'case=nom'];
//
// Note that in the latter case, the order does not matter, and we do not
// need to specify a value for number: ordering is handled by the system, and
// for unspecified features it generates a disjunction matching all possible
// values of the feature.

// Feature specifications get translated into simple 2-state FSTs that recognize
// only the disjunction of the given feature and its values. Thus for example,
//
// gender = Feature['gender', 'mas', 'fem', 'neu'];
//
// will translate into
//
// 0   1   gender=mas
// 0   1   gender=fem
// 0   1   gender=neu
// 1
//
// The labels "gender=mas", etc. are added to the GenerateLabels (stringfst.h).
//
// A Category is simply a sequence of Features, and is represented by an
// automaton that has N+1 states, where N is the number of features.
//
// Thus
//
// noun = Category[case, number, gender];
//
// will become:
//
// 0   1   case=nom
// 0   1   case=gen
// 0   1   case=dat
// 0   1   case=acc
// 1   2   gender=mas
// 1   2   gender=fem
// 1   2   gender=neu
// 2   3   number=sg
// 2   3   number=pl
// 3
//
// Note that the ordering is determined based on the lexicographic order of the
// feature. No matter what order we specify in the call to Category[], the order
// is always "case", "gender", "number". Thus the following are all equivalent
// to the above:
//
// noun = Category[number, gender, case];
// noun = Category[case, gender, number];
// etc.
//
// Category checks to make sure that each acceptor passed to it is a valid
// Feature acceptor meaning that it must have 2 states, be acyclic, and have
// arcs labeled with labels of the form "x=y", where all the "x"s must be the
// same.
//
// A FeatureVector allows one to specify a set of features for a given
// category. The first argument is the previously defined Category
// acceptor. FeatureVector checks that the first argument is a valid Category
// acceptor, and that the other arguments are strings that represent valid
// features for that category.
//
// Feature optionally allows the last argument to be a parse mode ('byte',
// 'utf8', or a symbol table). The default is 'byte'. This does not affect the
// parsing of the feature specification, but if you set FST_FLAGS_save_symbols then
// you will need to use this to make sure the feature acceptors have appropriate
// symbol tables for combining with other fsts.

#ifndef THRAX_FEATURES_H_
#define THRAX_FEATURES_H_

#include <stddef.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fst/compat.h>
#include <thrax/compat/compat.h>
#include <fst/concat.h>
#include <fst/rmepsilon.h>
#include <fst/string.h>
#include <thrax/fst-node.h>
#include <thrax/datatype.h>
#include <thrax/function.h>
#include <thrax/stringfst.h>
#include <thrax/symbols.h>

DECLARE_bool(save_symbols);  // From util/flags.cc.

namespace thrax {
namespace function {

static const char kFeatureEquals[] = "=";

// Splits out the prefix in a string of the form "x=y", and returns true on
// success or false on failure.
inline bool SplitFeatureValue(const std::string& featval,
                              std::string* feature) {
  int eq = featval.find(kFeatureEquals);
  if (eq == std::string::npos || eq == 0) return false;
  *feature = featval.substr(0, eq);
  return true;
}

template <typename Arc>
class Feature : public Function<Arc> {
 public:
  using MutableTransducer = ::fst::VectorFst<Arc>;

  Feature() : Function<Arc>() {}
  ~Feature() final {}

 protected:
  std::unique_ptr<DataType> Execute(
      const std::vector<std::unique_ptr<DataType>>& args) final {
    CHECK_GE(args.size(), 2);
    enum ::fst::TokenType mode = ::fst::TokenType::BYTE;
    const ::fst::SymbolTable* symtab = nullptr;
    // First argument is the name of the feature
    if (!args[0]->is<std::string>()) {
      std::cout << "Feature: First argument must be string" << std::endl;
      return nullptr;
    }
    const auto& feature_name = *args[0]->get<std::string>();
    std::vector<std::string> feature_values;
    // Subsequent arguments are the names of the values, but the last argument
    // could be ... see below
    for (int i = 1; i < args.size() - 1; ++i) {
      if (!args[i]->is<std::string>()) {
        std::cout << "Feature: All arguments except last must be strings (arg "
                  << i + 1 << ")" << std::endl;
        return nullptr;
      }
      feature_values.push_back(*args[i]->get<std::string>());
    }
    // Last argument can either be a feature specification, or 'byte', 'utf8',
    // or a symbol table. If any of the latter, then also check that
    // args.size() > 2, since we do not want to allow something like
    //
    // Feature['case', mysyms];
    //
    // Note that this does not affect the parsing, but if one specifies
    // FST_FLAGS_save_symbols, and one wishes to combine this Feature, or derived
    // Categories or FeatureVectors, with FSTs that are compiled with something
    // other than byte mode, then one must specify 'utf8' or a user-provided
    // symbol table so that the symbol tables will match.
    size_t i = args.size() - 1;
    if (args[i]->is<::fst::SymbolTable>()) {
      CHECK_GE(args.size(), 3);
      symtab = args[i]->get<::fst::SymbolTable>();
      mode = ::fst::TokenType::SYMBOL;
    } else if (args[i]->is<std::string>()) {
      const auto& arg = *args[i]->get<std::string>();
      if (arg == "utf8") {
        mode = ::fst::TokenType::UTF8;
        if (FST_FLAGS_save_symbols) symtab = GetUtf8SymbolTable();
      } else if (arg == "byte") {
        mode = ::fst::TokenType::BYTE;
        if (FST_FLAGS_save_symbols) symtab = GetByteSymbolTable();
      } else {
        feature_values.push_back(arg);
      }
    } else {
      std::cout << "Feature: final argument must be string or symbol table"
                << std::endl;
    }
    auto fst = std::make_unique<MutableTransducer>();
    // Feature acceptors have two states, with 0 the initial, 1, the final, and
    // all arcs going from 0 to 1.
    auto s0 = fst->AddState();
    fst->SetStart(s0);
    auto s1 = fst->AddState();
    fst->SetFinal(s1);
    for (int i = 0; i < feature_values.size(); ++i) {
      std::string feature_value_pair =
          feature_name + kFeatureEquals + feature_values[i];
      // Adds feature value pair of the form "x=y" to the generated labels
      // (stringfst.h)
      AddLabelForFeatureValuePair(feature_value_pair);
      int64_t label;
      if (!StringFst<Arc>::SymbolToGeneratedLabel(
              feature_value_pair, &label)) {
        std::cout << "Failed to generate label for " << feature_value_pair
                  << std::endl;
        return nullptr;
      }
      fst->EmplaceArc(s0, label, label, s1);
    }
    if (FST_FLAGS_save_symbols) {
      if (symtab) {
        fst->SetInputSymbols(symtab);
        fst->SetOutputSymbols(symtab);
      } else if (mode == ::fst::TokenType::UTF8) {
        fst->SetInputSymbols(GetUtf8SymbolTable());
        fst->SetOutputSymbols(GetUtf8SymbolTable());
      } else {
        fst->SetInputSymbols(GetByteSymbolTable());
        fst->SetOutputSymbols(GetByteSymbolTable());
      }
    }
    return std::make_unique<DataType>(std::move(fst));
  }

 public:
  // Validate the fst as an valid feature/value sequence automaton.
  // A valid feature/value sequence automaton has the following properties:
  //
  // 1) The fst is an acyclic acceptor
  // 2) All arcs from state si must go to the same sj
  // 3) All arcs are labeled with labels that have the form x=y, where the
  //    prefix x must be the same for all labels from a given state.
  //
  // This is generalized to handle Categories, which are just concatenated
  // feature automata, hence the generality here.
  //
  // In addition to verifying well-formedness, the function puts pairs of
  // stateid and feature name into the vector 'features'. This is useful for
  // other processes that want to know which state is associated with which
  // feature.
  static bool ValidateFeatureSequenceFst(
      MutableTransducer* fst,
      std::vector<std::pair<typename Arc::StateId, std::string>>* features) {
    if (!fst->Properties(::fst::kAcceptor | ::fst::kAcyclic, true)) {
      std::cout
          << "Feature/value sequence automaton must be an acyclic acceptor"
          << std::endl;
      return false;
    }
    typename Arc::StateId s = fst->Start();
    if (fst->Final(s) != Arc::Weight::Zero()) {
      std::cout
          << "Feature/value sequence automaton's initial state cannot be final"
          << std::endl;
      return false;
    }
    std::unique_ptr<::fst::SymbolTable> generated_symbols(
        StringFst<Arc>::GetLabelSymbolTable(false));
    while (fst->Final(s) == Arc::Weight::Zero()) {
      if (fst->NumArcs(s) <= 0) {
        std::cout
            << "Feature/value sequence automaton must have at least one arc "
            << "from every state" << std::endl;
        return false;
      }
      ::fst::ArcIterator<MutableTransducer> aiter(*fst, s);
      std::string feature = "";
      auto nextstate = ::fst::kNoStateId;
      while (!aiter.Done()) {
        const auto& arc = aiter.Value();
        if (nextstate == ::fst::kNoStateId) {
          nextstate = arc.nextstate;
        } else if (nextstate != arc.nextstate) {
          std::cout
              << "Every arc in a feature/value sequence automaton from state "
              << "s1 must transition to the same s0" << std::endl;
          return false;
        }
        std::string featval = generated_symbols->Find(arc.ilabel);
        std::string this_feature;
        if (!SplitFeatureValue(featval, &this_feature)) {
          std::cout << "Feature/value automaton's arcs must all "
                    << "be of the form x=y (" << featval << ")" << std::endl;
          return false;
        }
        // The first arc we look at on this state, figure out what the feature
        // is. Then all subsequent arcs must have the same feature.
        if (feature.empty()) {
          feature = this_feature;
        } else {
          if (feature != this_feature) {
            std::cout << "The feature name must be the same for all values ("
                      << featval << ")" << std::endl;
            return false;
          }
        }
        aiter.Next();
      }
      features->emplace_back(s, feature);
      s = nextstate;
    }
    if (fst->NumArcs(s) > 0) {
      std::cout
          << "The final state of a feature/value sequence automaton must not "
          << "have any exiting arcs" << std::endl;
      return false;
    }
    return true;
  }

  // Special case of above, for Features.
  // Validate the fst as an valid feature/value automaton.
  // In addition to the above, the fst must have exactly two states.
  // This puts into the string 'feature', the value of the feature that is
  // shared across the arcs.
  static bool ValidateFeatureFst(MutableTransducer* fst, std::string* feature) {
    if (fst->NumStates() != 2) {
      std::cout << "Feature/value automaton must have exactly two states"
                << std::endl;
      return false;
    }
    std::vector<std::pair<typename Arc::StateId, std::string>> features;
    if (ValidateFeatureSequenceFst(fst, &features)) {
      *feature = features[0].second;
      return true;
    }
    return false;
  }

 private:
  inline void AddLabelForFeatureValuePair(
      const std::string& feature_value_pair) {
    // Sole purpose is to add the bracketed_feature_value_pair to the generated
    // label set (stringfst.h)
    std::string bracketed_feature_value_pair = "[" + feature_value_pair + "]";
    StringFst<Arc> func;
    auto args = std::make_unique<std::vector<std::unique_ptr<DataType>>>();
    args->push_back(
        std::make_unique<DataType>(static_cast<int>(StringFstNode::BYTE)));
    args->push_back(std::make_unique<DataType>(bracketed_feature_value_pair));
    func.Run(std::move(args));
  }

  Feature<Arc>(const Feature<Arc>&) = delete;
  Feature<Arc>& operator=(const Feature<Arc>&) = delete;
};

// A category is a (sorted) sequence of feature/value pairs

template <typename Arc>
class Category : public Function<Arc> {
 public:
  using MutableTransducer = ::fst::VectorFst<Arc>;
  using Transducer = ::fst::Fst<Arc>;

  Category() {}
  ~Category() final {}

 protected:
  std::unique_ptr<DataType> Execute(
      const std::vector<std::unique_ptr<DataType>>& args) final {
    CHECK_GE(args.size(), 1);
    std::vector<std::pair<std::string, std::unique_ptr<Transducer>>> features;
    for (int i = 0; i < args.size(); ++i) {
      if (!args[i]->is<Transducer*>()) {
        std::cout << "Category: All arguments must be Feature fsts (arg "
                  << i + 1 << ")" << std::endl;
        return nullptr;
      }
      auto feature_fst =
          std::make_unique<MutableTransducer>(**args[i]->get<Transducer*>());
      std::string feature_name;
      CHECK(Feature<Arc>::ValidateFeatureFst(feature_fst.get(), &feature_name));
      features.emplace_back(feature_name, std::move(feature_fst));
    }
    // Features in a category are kept sorted by the byte sort order of the
    // feature name.
    std::sort(features.begin(), features.end());
    auto fst = std::make_unique<MutableTransducer>();
    const auto s0 = fst->AddState();
    fst->SetStart(s0);
    fst->SetFinal(s0);
    // Then concatenate each transducer in the sorted order of the associated
    // feature name.
    for (int i = 0; i < features.size(); ++i) {
      ::fst::Concat(fst.get(), *(features[i].second));
    }
    ::fst::RmEpsilon(fst.get());
    if (FST_FLAGS_save_symbols) {
      fst->SetInputSymbols(features[0].second->InputSymbols());
      fst->SetOutputSymbols(features[0].second->OutputSymbols());
    }
    return std::make_unique<DataType>(std::move(fst));
  }

 public:
  // See comments for ValidateFeatureSequenceFst under Feature
  static bool ValidateFeatureSequenceFst(
      MutableTransducer* fst,
      std::vector<std::pair<typename Arc::StateId, std::string>>* features) {
    return Feature<Arc>::ValidateFeatureSequenceFst(fst, features);
  }

 private:
  Category<Arc>(const Category<Arc>&) = delete;
  Category<Arc>& operator=(const Category<Arc>&) = delete;
};

// A FeatureVector is a particular instantiation for a Category, that allows the
// grammar writer to leave some of the features unspecified.

template <typename Arc>
class FeatureVector : public Function<Arc> {
 public:
  using MutableTransducer = ::fst::VectorFst<Arc>;
  using Transducer = ::fst::Fst<Arc>;

  FeatureVector() : Function<Arc>() {}
  ~FeatureVector() override {}

 protected:
  std::unique_ptr<DataType> Execute(
      const std::vector<std::unique_ptr<DataType>>& args) final {
    // In the minimal case, there are no specifications in this FeatureVector,
    // in which case we just get back an acceptor equivalent to the Category.
    CHECK_GE(args.size(), 1);
    if (!args[0]->is<Transducer*>()) {
      std::cout << "FeatureVector: First argument must be a Category fst"
                << std::endl;
      return nullptr;
    }
    auto* category_fst =
        fst::down_cast<MutableTransducer*>(*args[0]->get<Transducer*>());
    std::vector<std::pair<typename Arc::StateId, std::string>> features;
    if (!Category<Arc>::ValidateFeatureSequenceFst(category_fst,
                                                   &features)) {
      std::cout << "FeatureVector: First argument must be a Category fst"
                << std::endl;
      return nullptr;
    }
    // There should be no more feature value pairs than possible for this
    // category. Since the first argument is the category, and the category's
    // number of states is the number of feature value pairs plus one, this is
    // the right comparison
    if (args.size() > category_fst->NumStates()) {
      std::cout << "Too many feature/value pairs specified" << std::endl;
      return nullptr;
    }
    std::unique_ptr<::fst::SymbolTable> generated_symbols(
        StringFst<Arc>::GetLabelSymbolTable(false));
    std::map<std::string, int64_t> feature_label_pairs;
    for (int i = 1; i < args.size(); ++i) {
      if (!args[i]->is<std::string>()) {
        std::cout << "Feature/value pairs must strings be of the form x=y"
                  << std::endl;
        return nullptr;
      }
      auto featval = *args[i]->get<std::string>();
      std::string feature;
      if (!SplitFeatureValue(featval, &feature)) {
        std::cout << "Feature/value pairs must strings be of the form x=y: "
                  << featval << std::endl;
        return nullptr;
      }
      const int64_t label = generated_symbols->Find(featval);
      if (label == ::fst::kNoSymbol) {
        std::cout << "Feature/value pair " << featval << " is not defined."
                  << std::endl;
        return nullptr;
      }
      const auto it = feature_label_pairs.find(feature);
      if (it == feature_label_pairs.end()) {
        feature_label_pairs[feature] = label;
      } else {
        std::cout << "Duplicate value for feature: " << feature << std::endl;
        return nullptr;
      }
    }
    auto fst = std::make_unique<MutableTransducer>();
    auto s = fst->AddState();
    // We now craft the FST by looking at each feature-value pair, and making a
    // single arc in the sequence for that, but if a feature has no
    // specification in this FeatureVector, then we put in all arcs
    // corresponding to all possible specifications of the feature.
    fst->SetStart(s);
    for (const auto& pair : features) {
      // State in original category fst corresponding to this feature
      const auto feat_s = pair.first;
      const auto& feature = pair.second;
      const auto it = feature_label_pairs.find(feature);
      auto next = fst->AddState();
      if (it == feature_label_pairs.end()) {
        for (::fst::ArcIterator<MutableTransducer> aiter(*category_fst,
                                                             feat_s);
             !aiter.Done(); aiter.Next()) {
          const auto& arc = aiter.Value();
          fst->EmplaceArc(s, arc.ilabel, arc.olabel, next);
        }
      } else {
        const auto label = it->second;
        fst->EmplaceArc(s, label, label, next);
      }
      s = next;
    }
    fst->SetFinal(s);
    if (FST_FLAGS_save_symbols) {
      fst->SetInputSymbols(category_fst->InputSymbols());
      fst->SetOutputSymbols(category_fst->OutputSymbols());
    }
    return std::make_unique<DataType>(std::move(fst));
  }

 private:
  FeatureVector<Arc>(const FeatureVector<Arc>&) = delete;
  FeatureVector<Arc>& operator=(const FeatureVector<Arc>&) = delete;
};

}  // namespace function
}  // namespace thrax

#endif  // THRAX_FEATURES_H_
