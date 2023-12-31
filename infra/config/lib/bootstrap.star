# Copyright 2021 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Library for handling details for bootstrapping builder properties.

Enabling bootstrapping for a builder provides versioning of the properties:
property changes for CQ builders will be applied during the CQ run and when
building a revision of code, the properties that are applied will be the
properties that were set for the builder at that revision.

This is accomplished by the use of a passthrough-style bootstrapper luciexe. The
properties specified in the starlark for the builder are written out to a
separate file. The builder's definition in cr-buildbucket.cfg is updated to use
the bootstrapper executable and the builder is updated to use the bootstrapper
and have properties set that the bootstrapper consumes. These properties enable
the bootstrapper to apply the properties from the properties file and then
execute the exe that was specified for the builder.

To enable bootstrapping for a builder, its recipe must appear in
_BOOTSTRAPPABLE_RECIPES. In order for an recipe to interoperate with the
bootstrapper it must meet the following conditions:
* chromium_bootstrap.update_gclient_config is called to update the gclient
  config that is used for bot_update.
* If the recipe does analysis to reduce compilation/testing, it skips analysis
  and performs a full build if chromium_bootstrap.skip_analysis_reasons is
  non-empty.

To enable bootstrapping for a builder, set bootstrap=True in the builder
definition.
"""

load("@stdlib//internal/graph.star", "graph")
load("//project.star", "settings")

# builder_config.star has a generator that modifies properties, so load it first
# to ensure that the modified properties get written out to the property files
load("./builder_config.star", _ = "builder_config")  # @unused

# A recipe can (but doesn't have to) be marked as bootstrappable if the
# following conditions are true:
# * chromium_bootstrap.update_gclient_config is called to update the gclient
#   config that is used for bot_update.
# * If the recipe does analysis to reduce compilation/testing, it skips analysis
#   and performs a full build if chromium_bootstrap.skip_analysis_reasons is
#   non-empty.
_BOOTSTRAPPABLE_RECIPES = [
    "recipe:chromium",
    "recipe:chromium/orchestrator",
    "recipe:chromium_trybot",
]

_NON_BOOTSTRAPPED_PROPERTIES = [
    # Sheriff-o-Matic queries for builder_group in the input properties to find
    # builds for the main sheriff rotation. Bootstrapped properties don't appear
    # in the build's input properties, so don't bootstrap this property.
    # TODO(gbeaty) When finalized input properties are exported to BQ, remove
    # this.
    "builder_group",
    "sheriff_rotations",
]

def _bootstrap_key(bucket_name, builder_name):
    return graph.key("@chromium", "", "bootstrap", "{}/{}".format(bucket_name, builder_name))

def register_bootstrap(bucket, name, bootstrap, executable):
    """Registers the bootstrap for a builder.

    Args:
        bucket: The name of the bucket the builder belongs to.
        name: The name of the builder.
        bootstrap: Whether or not the builder should actually be bootstrapped.
        executable: The builder's executable.
    """

    # The properties file will be generated for any bootstrappable recipe so
    # that if a builder is switched to be bootstrapped its property file exist
    # even if an earlier revision is built (to a certain point). The bootstrap
    # property of the node will determine whether the builder's properties are
    # overwritten to actually use the bootstrapper.
    if executable in _BOOTSTRAPPABLE_RECIPES:
        graph.add_node(_bootstrap_key(bucket, name), props = {"bootstrap": bootstrap})

def _bootstrap_properties(ctx):
    """Update builder properties for bootstrapping.

    For builders whose recipe supports bootstrapping, their properties will be
    written out to a separate file. This is done even if the builder is not
    being bootstrapped so that the properties file will exist already when it is
    flipped to being bootstrapped.

    For builders that have opted in to bootstrapping, this file will be read at
    build-time and update the build's properties with the contents of the file.
    The builder's properties within the buildbucket configuration will be
    modified with the properties that control the bootstrapper itself.

    The builders that have opted in to bootstrapping is determined by examining
    the lucicfg graph to find a bootstrap node for a given builder. These nodes
    will be added by the builder function. This is done rather than writing out
    the properties file in the builder function so that the bootstrapped
    properties have any final modifications that luci.builder would perform
    (merging module-level defaults, setting global defaults, etc.).
    """
    cfg = None
    for f in ctx.output:
        if f.startswith("luci/cr-buildbucket"):
            cfg = ctx.output[f]
            break
    if cfg == None:
        fail("There is no buildbucket configuration file to reformat properties")

    for bucket in cfg.buckets:
        bucket_name = bucket.name
        for builder in bucket.swarming.builders:
            builder_name = builder.name
            bootstrap_node = graph.node(_bootstrap_key(bucket_name, builder_name))
            if not bootstrap_node:
                continue

            bootstrap = bootstrap_node.props.bootstrap

            properties_file = "builders/{}/{}/properties.textpb".format(bucket_name, builder_name)
            non_bootstrapped_properties = {
                "$bootstrap/properties": {
                    "top_level_project": {
                        "repo": {
                            "host": "chromium.googlesource.com",
                            "project": "chromium/src",
                        },
                        "ref": settings.ref,
                    },
                    "properties_file": "infra/config/generated/{}".format(properties_file),
                },
                "$bootstrap/exe": {
                    "exe": builder.exe,
                },
                "led_builder_is_bootstrapped": True,
            }
            builder_properties = json.decode(builder.properties)
            for p in _NON_BOOTSTRAPPED_PROPERTIES:
                if p in builder_properties:
                    non_bootstrapped_properties[p] = builder_properties.pop(p)
            ctx.output[properties_file] = json.indent(json.encode(builder_properties), indent = "  ")

            if bootstrap:
                builder.properties = json.encode(non_bootstrapped_properties)

                builder.exe.cipd_package = "infra/chromium/bootstrapper/${platform}"
                builder.exe.cipd_version = "latest"
                builder.exe.cmd = ["bootstrapper"]

lucicfg.generator(_bootstrap_properties)
