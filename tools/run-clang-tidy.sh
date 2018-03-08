# Copyright (C) 2018 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Let's not run this against dirty working trees for now.
git diff-index --quiet HEAD -- ||
  echo "Untracked changes. Stopping. Please commit your changes"; exit 1;

cd "$(dirname "$0")/..";
affected_files=$(python tools/get-affected-files.py '../../' | grep '.cc$');
cd "$(dirname "$0")";

if [[ ! -z "${affected_files// }" ]]; then
  gn gen clang_tidy_build \
    --args="is_debug=true is_hermetic_clang=false use_custom_libcxx=false";
  cd clang_tidy_build;
  cp ../../.clang-tidy .;
  ninja -t compdb cc cxx obj objcxx > compile_commands.json;
  # We need to build in order to generate proto headers.
  ninja;
  echo $affected_files | xargs -P100 ~/buildclang/build/bin/clang-tidy \
    '-header-filter=^'"$(realpath ../../)"'/(?!buildtools).*' -p=. -fix;
else
  echo "No .cc files affected."
fi
