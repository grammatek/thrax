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
#include <thrax/algo/stringcompile.h>

namespace fst {
namespace internal {

StringCompiler *StringCompiler::Get() {
  static auto *kInstance = new StringCompiler();
  return kInstance;
}

// Returns kNoLabel on failure.
int64 StringCompiler::NumericalSymbolToLabel(const std::string &token) const {
  const auto *ctoken = token.c_str();
  char *p;
  const auto label = strtoll(ctoken, &p, 0);
  return p < ctoken + token.size() ? kNoLabel : label;
}

int64 StringCompiler::StringSymbolToLabel(const std::string &token) {
  // Is a single byte.
  if (token.size() == 1) return *token.c_str();
  // Special handling for BOS and EOS markers in CDRewrite.
  if (token == kBosString) return kBosIndex;
  if (token == kEosString) return kEosIndex;
  // NB: If one were to make this thread-compatible, one would merely need to
  // grab a lock here and release it at the end of the function.
  // General symbol lookup.
  const auto label = generated_.AddSymbol(token, max_generated_);
  if (label == max_generated_) ++max_generated_;
  return label;
}

// Tries numerical parsing first, and if that fails, treats it as a generated
// label.
int64 StringCompiler::NumericalOrStringSymbolToLabel(const std::string &token) {
  int64 label = NumericalSymbolToLabel(token);
  if (label == kNoLabel) label = StringSymbolToLabel(token);
  return label;
}

// We store generated symbol numbering in the private areas in planes 15-16.
// There are roughly 130,000 such code points in this area.
StringCompiler::StringCompiler()
    : generated_(kGeneratedSymbolsName), max_generated_(0xF0000) {
  generated_.AddSymbol(kEpsilonString);
}

void StringCompiler::Reset() {
  // This is duplicated from the above constructor.
  generated_ = SymbolTable(kGeneratedSymbolsName);
  generated_.AddSymbol(kEpsilonString);
  max_generated_ = 0xF0000;
}

bool StringCompiler::MergeIntoGeneratedSymbols(const SymbolTable &symtab,
                                               std::map<int64, int64> *remap) {
  if (remap == nullptr) {
    LOG(WARNING) << "Must provide a non-null remap";
    return false;
  }
  bool success = true;
  for (const auto &item : symtab) {
    const int64 label = item.Label();
    const std::string symbol = item.Symbol();

    // Checks to see if we already have this label paired with this
    // symbol. FSTs associated with the incoming symbol table will get
    // remapped as needed.
    // Four possible outcomes:
    // 1) Neither label nor symbol exist: insert this new pair.
    // 2) Label exists but mapped to another symbol: generate new_label for
    //    the symbol, and add <label, new_label> to the remapping table.
    // 3) Symbol exists but with another old_label: reassign to old_label and
    //    add <label, old_label> to the remapping table.
    // 4) Both label and symbol exist: then we need to ask whether they have
    //    the same mapping.
    const auto slx = generated_.Find(symbol);
    const auto lsx = generated_.Find(label);
    if (slx == kNoSymbol && lsx.empty()) {
      // Case 1: Both new
      generated_.AddSymbol(symbol, label);
      VLOG(2) << "Loaded symbol " << symbol << " with label " << label;
      // On success, keeps track of the maximum + 1 for the next available
      // label.
      if (max_generated_ <= label) max_generated_ = label + 1;
    } else if (slx == kNoSymbol) {
      // Case 2: symbol is new, but label is there and therefore mapped to
      // something else.
      int64 new_label = max_generated_++;
      generated_.AddSymbol(symbol, new_label);

      remap->emplace(label, new_label);
      VLOG(2) << "Remapping " << symbol << " to new label " << new_label;
    } else if (lsx.empty()) {
      // Case 3: label is new, but symbol is there and therefore mapped to
      // something else.
      const int64 old_label = slx;
      remap->emplace(label, old_label);
      VLOG(2) << "Remapping " << symbol << " to old label " << old_label;
    } else {
      // Case 4: Both symbol and label already exist.
      const std::string &old_symbol = lsx;
      const int64 old_label = slx;
      if (symbol == old_symbol && label == old_label) {
        // Same, so ok and nothing to do.
        continue;
      } else if (label == old_label || symbol == old_symbol) {
        // symbol -> label gets you the right label, but label -> symbol
        // doesn't, or vice versa. This should not happen.
        LOG(WARNING) << "Detected label mismatch: " << symbol << " -> "
                     << old_label << ", " << label << " -> " << old_symbol;
        success = false;
      } else {
        // Both are there but assigned to other things.
        remap->emplace(label, old_label);
        VLOG(2) << "Remapping " << symbol << " to old label " << old_label;
      }
    }
  }
  return success;
}

}  // namespace internal

// Convenience methods, to eliminate the need to call Get on the singleton.

const SymbolTable &GeneratedSymbols() {
  static auto *compiler = internal::StringCompiler::Get();
  return compiler->GeneratedSymbols();
}

namespace thrax_internal {

bool MergeIntoGeneratedSymbols(const SymbolTable &symtab,
                               std::map<int64, int64> *remap) {
  static auto *compiler = internal::StringCompiler::Get();
  return compiler->MergeIntoGeneratedSymbols(symtab, remap);
}

void ResetGeneratedSymbols() {
  static auto *compiler = internal::StringCompiler::Get();
  compiler->Reset();
}

}  // namespace thrax_internal

}  // namespace fst
