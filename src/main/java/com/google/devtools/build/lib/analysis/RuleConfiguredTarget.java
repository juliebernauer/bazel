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
package com.google.devtools.build.lib.analysis;

import com.google.common.collect.ImmutableCollection;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.devtools.build.lib.analysis.config.ConfigMatchingProvider;
import com.google.devtools.build.lib.analysis.config.RunUnder;
import com.google.devtools.build.lib.cmdline.Label;
import com.google.devtools.build.lib.packages.OutputFile;
import com.google.devtools.build.lib.packages.Rule;
import com.google.devtools.build.lib.util.Preconditions;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * A {@link ConfiguredTarget} that is produced by a rule.
 *
 * <p>Created by {@link RuleConfiguredTargetBuilder}. There is an instance of this class for every
 * analyzed rule. For more information about how analysis works, see
 * {@link com.google.devtools.build.lib.rules.RuleConfiguredTargetFactory}.
 */
public final class RuleConfiguredTarget extends AbstractConfiguredTarget {
  /**
   * The configuration transition for an attribute through which a prerequisite
   * is requested.
   */
  public enum Mode {
    TARGET,
    HOST,
    DATA,
    SPLIT,
    DONT_CHECK
  }

  private final ImmutableMap<Class<? extends TransitiveInfoProvider>, Object> providers;
  private final ImmutableMap<Label, ConfigMatchingProvider> configConditions;

  RuleConfiguredTarget(RuleContext ruleContext,
      Map<Class<? extends TransitiveInfoProvider>, TransitiveInfoProvider> providers,
      SkylarkProviders skylarkProviders1) {
    super(ruleContext);
    // We don't use ImmutableMap.Builder here to allow augmenting the initial list of 'default'
    // providers by passing them in.
    Map<Class<? extends TransitiveInfoProvider>, Object> providerBuilder = new LinkedHashMap<>();
    providerBuilder.putAll(providers);
    Preconditions.checkState(providerBuilder.containsKey(RunfilesProvider.class));
    Preconditions.checkState(providerBuilder.containsKey(FileProvider.class));
    Preconditions.checkState(providerBuilder.containsKey(FilesToRunProvider.class));

    // Initialize every SkylarkApiProvider
    skylarkProviders1.init(this);
    providerBuilder.put(SkylarkProviders.class, skylarkProviders1);

    this.providers = ImmutableMap.copyOf(providerBuilder);
    this.configConditions = ruleContext.getConfigConditions();

    // If this rule is the run_under target, then check that we have an executable; note that
    // run_under is only set in the target configuration, and the target must also be analyzed for
    // the target configuration.
    RunUnder runUnder = getConfiguration().getRunUnder();
    if (runUnder != null && getLabel().equals(runUnder.getLabel())) {
      if (getProvider(FilesToRunProvider.class).getExecutable() == null) {
        ruleContext.ruleError("run_under target " + runUnder.getLabel() + " is not executable");
      }
    }

    // Make sure that all declared output files are also created as artifacts. The
    // CachingAnalysisEnvironment makes sure that they all have generating actions.
    if (!ruleContext.hasErrors()) {
      for (OutputFile out : ruleContext.getRule().getOutputFiles()) {
        ruleContext.createOutputArtifact(out);
      }
    }
  }

  /**
   * The configuration conditions that trigger this rule's configurable attributes.
   */
  ImmutableMap<Label, ConfigMatchingProvider> getConfigConditions() {
    return configConditions;
  }

  @Override
  public <P extends TransitiveInfoProvider> P getProvider(Class<P> providerClass) {
    AnalysisUtils.checkProvider(providerClass);
    // TODO(bazel-team): Should aspects be allowed to override providers on the configured target
    // class?
    Object provider = providers.get(providerClass);
    if (provider == null) {
      return null;
    } else {
      return providerClass.cast(provider);
    }
  }

  /**
   * Returns a value provided by this target. Only meant to use from Skylark.
   */
  @Override
  public Object get(String providerKey) {
    return getProvider(SkylarkProviders.class).getValue(providerKey);
  }

  @Override
  public final Rule getTarget() {
    return (Rule) super.getTarget();
  }

  @Override
  public String errorMessage(String name) {
    return String.format("target (rule class of '%s') doesn't have provider '%s'.",
        getTarget().getRuleClass(), name);
  }

  @Override
  public ImmutableCollection<String> getKeys() {
    return ImmutableList.<String>builder().addAll(super.getKeys())
        .addAll(getProvider(SkylarkProviders.class).getKeys()).build();
  }
}
