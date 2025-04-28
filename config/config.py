import generator as sc
import sys

config_c, config_h = sc.config({
    "output-file": sc.string(),
    "verbose": sc.bool(),
    "png_compression_level": sc.int().require("0 <= x && x <= 9"),
    "move-to-background": sc.bool(),
    "copy-to-clipboard": sc.bool(),
    "notify": {
        "enabled": sc.bool()
    },
    "region": {
        # TODO: change this to sc.color() once that exists
        "border-color": sc.int() | sc.enum("smart")
    }
})

with open(sys.argv[1], "w") as f:
    f.write(config_c)

with open(sys.argv[2], "w") as f:
    f.write(config_h)
