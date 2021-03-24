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
#include <thrax/grammar-node.h>

#include <memory>

#include <thrax/collection-node.h>
#include <thrax/node.h>
#include <thrax/walker.h>

namespace thrax {

GrammarNode::GrammarNode(CollectionNode* imports,
                         CollectionNode* functions,
                         CollectionNode* statements)
    : Node(), imports_(imports), functions_(functions),
      statements_(statements) {}

CollectionNode* GrammarNode::GetImports() const { return imports_.get(); }

CollectionNode* GrammarNode::GetFunctions() const { return functions_.get(); }

CollectionNode* GrammarNode::GetStatements() const { return statements_.get(); }

void GrammarNode::Accept(AstWalker* walker) { walker->Visit(this); }

}  // namespace thrax
