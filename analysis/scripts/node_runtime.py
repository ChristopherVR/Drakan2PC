"""Environment for Node tools that load TypeScript directly."""

import os
import re


def node_environment(**overrides: str) -> dict[str, str]:
    env = dict(os.environ, **overrides)
    if options := env.get("NODE_OPTIONS"):
        # Match whole options, including quoted arguments, so other Node settings pass through.
        option = re.compile(r'(?:[^"\s]+|"(?:[^"\\]|\\.)*")+')
        disable_stripping = {"--no-experimental-strip-types", '"--no-experimental-strip-types"'}
        env["NODE_OPTIONS"] = option.sub(
            lambda match: "" if match[0] in disable_stripping else match[0], options
        ).strip()
    return env
