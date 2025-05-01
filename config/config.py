import generator as sc
import sys

config_c, config_h = sc.config({
    "output-file": sc.string(),
    "verbose": sc.bool(),
    "png-compression-level": sc.int().require("0 <= x && x <= 9"),
    "move-to-background": sc.bool(),
    "copy-to-clipboard": sc.bool(),
    "notify": {
        "enabled": sc.bool()
    },
    "region": {
        "selection-border-color": sc.color() | sc.enum("smart"),
        "selection-border-width": sc.length(),
        "background": sc.color(),
    }
})

with open(sys.argv[1], "w") as f:
    f.write(config_c)

with open(sys.argv[2], "w") as f:
    f.write(config_h)
