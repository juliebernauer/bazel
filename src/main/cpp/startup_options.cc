// Copyright 2014 The Bazel Authors. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "src/main/cpp/startup_options.h"

#include <assert.h>
#include <errno.h>  // errno, ENOENT
#include <string.h>  // strerror
#include <unistd.h>  // access

#include <cstdio>
#include <cstdlib>

#include "src/main/cpp/blaze_util_platform.h"
#include "src/main/cpp/blaze_util.h"
#include "src/main/cpp/util/exit_code.h"
#include "src/main/cpp/util/file.h"
#include "src/main/cpp/util/numbers.h"
#include "src/main/cpp/util/strings.h"

#ifndef PRODUCT_NAME
#define PRODUCT_NAME "Bazel"
#endif

namespace blaze {

using std::vector;

StartupOptions::StartupOptions() {
  Init();
}

StartupOptions::~StartupOptions() {
}

// TODO(jmmv): Integrate Init into the StartupOptions constructor.
void StartupOptions::Init() {
  bool testing = getenv("TEST_TMPDIR") != NULL;
  if (testing) {
    output_root = MakeAbsolute(getenv("TEST_TMPDIR"));
  } else {
    output_root = GetOutputRoot();
  }

  // TODO(jmmv): Now that we have per-product main.cc files, inject the
  // product_name at construction time instead of using preprocessor
  // definitions.
  product_name = PRODUCT_NAME;
  string product_name_lower = PRODUCT_NAME;
  blaze_util::ToLower(&product_name_lower);
  output_user_root = blaze_util::JoinPath(
      output_root, "_" + product_name_lower + "_" + GetUserName());
  deep_execroot = true;
  block_for_lock = true;
  host_jvm_debug = false;
  host_javabase = "";
  batch = false;
  batch_cpu_scheduling = false;
  allow_configurable_attributes = false;
  fatal_event_bus_exceptions = false;
  io_nice_level = -1;
  // 3 hours (but only 15 seconds if used within a test)
  max_idle_secs = testing ? 15 : (3 * 3600);
  oom_more_eagerly_threshold = 100;
  command_port = 0;
  oom_more_eagerly = false;
  watchfs = false;
  invocation_policy = NULL;
}

string StartupOptions::GetOutputRoot() {
  return blaze::GetOutputRoot();
}

void StartupOptions::AddExtraOptions(vector<string> *result) const {}

blaze_exit_code::ExitCode StartupOptions::ProcessArg(
      const string &argstr, const string &next_argstr, const string &rcfile,
      bool *is_space_separated, string *error) {
  // We have to parse a specific option syntax, so GNU getopts won't do.  All
  // options begin with "--" or "-". Values are given together with the option
  // delimited by '=' or in the next option.
  const char* arg = argstr.c_str();
  const char* next_arg = next_argstr.empty() ? NULL : next_argstr.c_str();
  const char* value = NULL;

  if ((value = GetUnaryOption(arg, next_arg, "--output_base")) != NULL) {
    output_base = MakeAbsolute(value);
    option_sources["output_base"] = rcfile;
  } else if ((value = GetUnaryOption(arg, next_arg,
                                     "--install_base")) != NULL) {
    install_base = MakeAbsolute(value);
    option_sources["install_base"] = rcfile;
  } else if ((value = GetUnaryOption(arg, next_arg,
                                     "--output_user_root")) != NULL) {
    output_user_root = MakeAbsolute(value);
    option_sources["output_user_root"] = rcfile;
  } else if (GetNullaryOption(arg, "--deep_execroot")) {
    deep_execroot = true;
    option_sources["deep_execroot"] = rcfile;
  } else if (GetNullaryOption(arg, "--nodeep_execroot")) {
    deep_execroot = false;
    option_sources["deep_execroot"] = rcfile;
  } else if (GetNullaryOption(arg, "--noblock_for_lock")) {
    block_for_lock = false;
    option_sources["block_for_lock"] = rcfile;
  } else if (GetNullaryOption(arg, "--host_jvm_debug")) {
    host_jvm_debug = true;
    option_sources["host_jvm_debug"] = rcfile;
  } else if ((value = GetUnaryOption(arg, next_arg,
                                     "--host_jvm_profile")) != NULL) {
    host_jvm_profile = value;
    option_sources["host_jvm_profile"] = rcfile;
  } else if ((value = GetUnaryOption(arg, next_arg,
                                     "--host_javabase")) != NULL) {
    // TODO(bazel-team): Consider examining the javabase and re-execing in case
    // of architecture mismatch.
    host_javabase = MakeAbsolute(value);
    option_sources["host_javabase"] = rcfile;
  } else if ((value = GetUnaryOption(arg, next_arg, "--host_jvm_args")) !=
             NULL) {
    host_jvm_args.push_back(value);
    option_sources["host_jvm_args"] = rcfile;  // NB: This is incorrect
  } else if ((value = GetUnaryOption(arg, next_arg, "--bazelrc")) != NULL) {
    if (rcfile != "") {
      *error = "Can't specify --bazelrc in the .bazelrc file.";
      return blaze_exit_code::BAD_ARGV;
    }
  } else if ((value = GetUnaryOption(arg, next_arg, "--blazerc")) != NULL) {
    if (rcfile != "") {
      *error = "Can't specify --blazerc in the .blazerc file.";
      return blaze_exit_code::BAD_ARGV;
    }
  } else if (GetNullaryOption(arg, "--nomaster_blazerc") ||
             GetNullaryOption(arg, "--master_blazerc")) {
    if (rcfile != "") {
      *error = "Can't specify --[no]master_blazerc in .blazerc file.";
      return blaze_exit_code::BAD_ARGV;
    }
    option_sources["blazerc"] = rcfile;
  } else if (GetNullaryOption(arg, "--nomaster_bazelrc") ||
             GetNullaryOption(arg, "--master_bazelrc")) {
    if (rcfile != "") {
      *error = "Can't specify --[no]master_bazelrc in .bazelrc file.";
      return blaze_exit_code::BAD_ARGV;
    }
    option_sources["blazerc"] = rcfile;
  } else if (GetNullaryOption(arg, "--batch")) {
    batch = true;
    option_sources["batch"] = rcfile;
  } else if (GetNullaryOption(arg, "--nobatch")) {
    batch = false;
    option_sources["batch"] = rcfile;
  } else if (GetNullaryOption(arg, "--batch_cpu_scheduling")) {
    batch_cpu_scheduling = true;
    option_sources["batch_cpu_scheduling"] = rcfile;
  } else if (GetNullaryOption(arg, "--nobatch_cpu_scheduling")) {
    batch_cpu_scheduling = false;
    option_sources["batch_cpu_scheduling"] = rcfile;
  } else if (GetNullaryOption(arg, "--allow_configurable_attributes")) {
    allow_configurable_attributes = true;
    option_sources["allow_configurable_attributes"] = rcfile;
  } else if (GetNullaryOption(arg, "--noallow_configurable_attributes")) {
    allow_configurable_attributes = false;
    option_sources["allow_configurable_attributes"] = rcfile;
  } else if (GetNullaryOption(arg, "--fatal_event_bus_exceptions")) {
    fatal_event_bus_exceptions = true;
    option_sources["fatal_event_bus_exceptions"] = rcfile;
  } else if (GetNullaryOption(arg, "--nofatal_event_bus_exceptions")) {
    fatal_event_bus_exceptions = false;
    option_sources["fatal_event_bus_exceptions"] = rcfile;
  } else if ((value = GetUnaryOption(arg, next_arg,
                                     "--io_nice_level")) != NULL) {
    if (!blaze_util::safe_strto32(value, &io_nice_level) ||
        io_nice_level > 7) {
      blaze_util::StringPrintf(error,
          "Invalid argument to --io_nice_level: '%s'. Must not exceed 7.",
          value);
      return blaze_exit_code::BAD_ARGV;
    }
    option_sources["io_nice_level"] = rcfile;
  } else if ((value = GetUnaryOption(arg, next_arg,
                                     "--max_idle_secs")) != NULL) {
    if (!blaze_util::safe_strto32(value, &max_idle_secs) ||
        max_idle_secs < 0) {
      blaze_util::StringPrintf(error,
          "Invalid argument to --max_idle_secs: '%s'.", value);
      return blaze_exit_code::BAD_ARGV;
    }
    option_sources["max_idle_secs"] = rcfile;
  } else if (GetNullaryOption(arg, "-x")) {
    fprintf(stderr, "WARNING: The -x startup option is now ignored "
            "and will be removed in a future release\n");
  } else if (GetNullaryOption(arg, "--experimental_oom_more_eagerly")) {
    oom_more_eagerly = true;
    option_sources["experimental_oom_more_eagerly"] = rcfile;
  } else if (GetNullaryOption(arg, "--noexperimental_oom_more_eagerly")) {
    oom_more_eagerly = false;
    option_sources["experimental_oom_more_eagerly"] = rcfile;
  } else if ((value = GetUnaryOption(
                  arg, next_arg,
                  "--experimental_oom_more_eagerly_threshold")) != NULL) {
    if (!blaze_util::safe_strto32(value, &oom_more_eagerly_threshold) ||
        oom_more_eagerly_threshold < 0) {
      blaze_util::StringPrintf(error,
                               "Invalid argument to "
                               "--experimental_oom_more_eagerly_threshold: "
                               "'%s'.",
                               value);
      return blaze_exit_code::BAD_ARGV;
    }
    option_sources["experimental_oom_more_eagerly_threshold"] = rcfile;
  } else if (GetNullaryOption(arg, "--watchfs")) {
    watchfs = true;
    option_sources["watchfs"] = rcfile;
  } else if (GetNullaryOption(arg, "--nowatchfs")) {
    watchfs = false;
    option_sources["watchfs"] = rcfile;
  } else if ((value = GetUnaryOption(
      arg, next_arg, "--command_port")) != NULL) {
    if (!blaze_util::safe_strto32(value, &command_port) ||
        command_port < -1 || command_port > 65535) {
      blaze_util::StringPrintf(error,
          "Invalid argument to --command_port: '%s'. "
          "Must be a valid port number or -1 to disable the gRPC server.\n",
          value);
      return blaze_exit_code::BAD_ARGV;
    }
    option_sources["webstatusserver"] = rcfile;
  } else if ((value = GetUnaryOption(arg, next_arg, "--invocation_policy"))
              != NULL) {
    if (invocation_policy == NULL) {
      invocation_policy = value;
      option_sources["invocation_policy"] = rcfile;
    } else {
      *error = "The startup flag --invocation_policy cannot be specified "
          "multiple times.";
      return blaze_exit_code::BAD_ARGV;
    }
  } else {
    bool extra_argument_processed;
    blaze_exit_code::ExitCode process_extra_arg_exit_code = ProcessArgExtra(
        arg, next_arg, rcfile, &value, &extra_argument_processed, error);
    if (process_extra_arg_exit_code != blaze_exit_code::SUCCESS) {
      return process_extra_arg_exit_code;
    }
    if (!extra_argument_processed) {
      blaze_util::StringPrintf(
          error,
          "Unknown %s startup option: '%s'.\n"
          "  For more info, run '%s help startup_options'.",
          product_name.c_str(), arg, product_name.c_str());
      return blaze_exit_code::BAD_ARGV;
    }
  }

  *is_space_separated = ((value == next_arg) && (value != NULL));
  return blaze_exit_code::SUCCESS;
}

blaze_exit_code::ExitCode StartupOptions::ProcessArgExtra(
    const char *arg, const char *next_arg, const string &rcfile,
    const char **value, bool *is_processed, string *error) {
  *is_processed = false;
  return blaze_exit_code::SUCCESS;
}

blaze_exit_code::ExitCode StartupOptions::CheckForReExecuteOptions(
      int argc, const char *argv[], string *error) {
  return blaze_exit_code::SUCCESS;
}

string StartupOptions::GetDefaultHostJavabase() const {
  return blaze::GetDefaultHostJavabase();
}

string StartupOptions::GetHostJavabase() {
  if (host_javabase.empty()) {
    host_javabase = GetDefaultHostJavabase();
  }
  return host_javabase;
}

string StartupOptions::GetJvm() {
  string java_program = GetHostJavabase() + "/bin/java";
  if (access(java_program.c_str(), X_OK) == -1) {
    if (errno == ENOENT) {
      fprintf(stderr, "Couldn't find java at '%s'.\n", java_program.c_str());
    } else {
      fprintf(stderr, "Couldn't access %s: %s\n", java_program.c_str(),
          strerror(errno));
    }
    exit(1);
  }
  // If the full JDK is installed
  string jdk_rt_jar = GetHostJavabase() + "/jre/lib/rt.jar";
  // If just the JRE is installed
  string jre_rt_jar = GetHostJavabase() + "/lib/rt.jar";
  if ((access(jdk_rt_jar.c_str(), R_OK) == 0)
      || (access(jre_rt_jar.c_str(), R_OK) == 0)) {
    return java_program;
  }
  fprintf(stderr, "Problem with java installation: "
      "couldn't find/access rt.jar in %s\n", GetHostJavabase().c_str());
  exit(1);
}

string StartupOptions::GetExe(const string &jvm, const string &jar_path) {
  return jvm;
}

void StartupOptions::AddJVMArgumentPrefix(const string &javabase,
    std::vector<string> *result) const {
}

void StartupOptions::AddJVMArgumentSuffix(const string &real_install_dir,
                                          const string &jar_path,
    std::vector<string> *result) const {
  result->push_back("-jar");
  result->push_back(blaze::ConvertPath(
      blaze_util::JoinPath(real_install_dir, jar_path)));
}

blaze_exit_code::ExitCode StartupOptions::AddJVMArguments(
    const string &host_javabase, vector<string> *result,
    const vector<string> &user_options, string *error) const {
  // Configure logging
  const string propFile = output_base + "/javalog.properties";
  if (!WriteFile(
      "handlers=java.util.logging.FileHandler\n"
      ".level=INFO\n"
      "java.util.logging.FileHandler.level=INFO\n"
      "java.util.logging.FileHandler.pattern="
      + output_base + "/java.log\n"
      "java.util.logging.FileHandler.limit=50000\n"
      "java.util.logging.FileHandler.count=1\n"
      "java.util.logging.FileHandler.formatter="
      "java.util.logging.SimpleFormatter\n",
      propFile)) {
    perror(("Couldn't write logging file " + propFile).c_str());
  } else {
    result->push_back("-Djava.util.logging.config.file=" + propFile);
  }
  return blaze_exit_code::SUCCESS;
}

blaze_exit_code::ExitCode StartupOptions::ValidateStartupOptions(
    const std::vector<string>& args, string* error) {
  return blaze_exit_code::SUCCESS;
}

}  // namespace blaze
