import generator as sc
import sys

config_c, config_h, config_vapi = sc.config({
    "output-file": sc.string(),
    "verbose": sc.bool(),
    "png-compression-level": sc.int().require("0 <= x && x <= 9"),
    "move-to-background": sc.bool(),
    "copy-to-clipboard": sc.bool(),
    "notify": {
        "enabled": sc.bool(),
        "summary": sc.string(),
        "body-copy": sc.string(),
        "body-nocopy": sc.string(),
        "edit-command": sc.string(),
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

with open(sys.argv[3], "w") as f:
    f.write(config_vapi)
