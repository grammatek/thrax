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
#ifndef FST_UTIL_STRING_STRINGUTIL_H_
#define FST_UTIL_STRING_STRINGUTIL_H_

#include <string>

namespace fst {

// Defines comment syntax for string files.
//
// The comment character is '#', and has scope until the end of the line. Any
// preceding whitespace before a comment is ignored.
//
// To use the '#' literal (i.e., to ensure it is not interpreted as the start of
// a comment) escape it with '\'; the escaping '\' in "\#" also removed.
std::string StripCommentAndRemoveEscape(const std::string &line);

// Escapes characters (namely, backslash and square brackets) used to indicate
// generated symbols.
std::string Escape(const std::string &str);

}  // namespace fst

#endif  // FST_UTIL_STRING_STRINGUTIL_H_
