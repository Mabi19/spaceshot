from __future__ import annotations
from abc import ABC, abstractmethod
from dataclasses import dataclass
import re

# Is this overengineered? Yes.
# Is this cursed? Very yes.
# Is this worth it? Definitely also yes.

# Quirks:
# - strings will not clean up properly when in variants (they aren't very useful in variants anyway)

#* The Declaration* classes should be expanded a little.
# The main struct is mapped to a Vala compact class, but sub-structs should be regular structs.
# I think the best way to handle this would be a flag passed into generate_vala.
# Also, in Vala, the Config prefix is implied by the context. It shouldn't be hard-coded in everywhere
# but instead added during generation.

def snake_case_to_pascal(x: str):
    return "".join(part.capitalize() for part in x.split("_"))

def pascal_to_snake_case(x: str):
    return re.sub(r'(?<!^)(?=[A-Z])', '_', x).lower()

def constantify_type_signature(ident: str):
    return ident.replace(".", "_").replace("'", "").upper()

class DeclarationType(ABC):
    @abstractmethod
    def generate_c(self) -> str | None:
        ...

    def get_dependent_types(self) -> list[DeclarationType]:
        return []

    @abstractmethod
    def get_c_name(self) -> str:
        ...

    @abstractmethod
    def get_vala_name(self) -> str:
        ...

@dataclass
class DeclarationSimpleType(DeclarationType):
    c_name: str
    vala_name: str | None = None
    def generate_c(self):
        return None

    def get_c_name(self):
        return self.c_name

    def get_vala_name(self):
        return self.vala_name or self.c_name

@dataclass
class DeclarationEnum(DeclarationType):
    name: str
    members: list[str]
    comment: str | None = None

    def generate_c(self):
        parts = []
        if self.comment:
            parts.append(f"/* {self.comment} */")
        parts.append(f"typedef enum {{")
        for member in self.members:
            enum_value = (pascal_to_snake_case(self.name) + "_" + member).upper()
            parts.append(f"    CONFIG_{enum_value};")
        parts.append(f"}} Config{self.name};\n")
        return "\n".join(parts)

    def get_c_name(self):
        return "Config" + self.name + " "

    def get_vala_name(self):
        return self.name + " "

@dataclass
class DeclarationStruct(DeclarationType):
    name: str
    props: dict[str, DeclarationType]
    comment: str | None = None

    def generate_c(self):
        parts = []
        if self.comment:
            parts.append(f"/* {self.comment} */")
        parts.append(f"typedef struct {{")
        for key, type in self.props.items():
            parts.append(f"    {type.get_c_name()}{key.replace("-", "_")};")
        parts.append(f"}} Config{self.name};\n")
        return "\n".join(parts)

    def get_dependent_types(self):
        return list(self.props.values())

    def get_c_name(self):
        return "Config" + self.name + " "

    def get_vala_name(self):
        return self.name + " "

@dataclass
class DeclarationVariantStruct(DeclarationType):
    '''A struct containing a type attribute of enum type, and its other members wrapped in a union.'''
    name: str
    options: list[str]
    props: dict[str, DeclarationType]

    def is_empty_struct(self):
        '''Whether this variant struct will be generated as a pure enum.'''
        return len(self.props) == 0

    def generate_c(self):
        if self.is_empty_struct():
            return DeclarationEnum(self.name, self.options).generate_c()

        parts = []
        parts.append(f"typedef enum {{")
        for option in self.options:
            enum_value = (pascal_to_snake_case(self.name) + "_" + option).upper()
            parts.append(f"    CONFIG_{enum_value};")
        parts.append(f"}} Config{self.name}Type;\n")

        parts.append(f"typedef struct {{")
        parts.append(f"    Config{self.name}Type type;")
        parts.append(f"    union {{")
        for key, type in self.props.items():
            parts.append(f"        {type.get_c_name()}{key.replace("-", "_")};")
        parts.append(f"    }};")
        parts.append(f"}} Config{self.name};\n")
        return "\n".join(parts)

    def get_dependent_types(self):
        return list(self.props.values())

    def get_c_name(self):
        return "Config" + self.name + " "

    def get_vala_name(self):
        return self.name + " "

class DeclarationContext:
    declarations: list[DeclarationType]

    def __init__(self):
        self.declarations = []

    def add(self, obj: DeclarationType):
        self.declarations.append(obj)

class BaseType(ABC):
    condition: str | None

    def __init__(self):
        super()
        self.condition = None

    def __or__(self, other):
        return variant(self, other)

    @abstractmethod
    def get_declaration_type(self, qualified_name: str) -> DeclarationType:
        ...

    @abstractmethod
    def get_parse_expr(self):
        '''Parse data from the value variable and store in the x variable'''
        ...

    def generate_parse_code(self, qualified_c_name: str, indent: str, run_on_success: str | None = None):
        '''Generate a block of code that assigns the value and returns if successful.'''
        def generate_condition_check(value: BaseType, indent: str):
            return f'''{indent}if (!({value.condition})) {{
{indent}    config_warn("value %s for key %s is invalid (needs to be {value.condition})", value, key);
{indent}    return;
{indent}}}'''

        return f"""{indent}{self.get_declaration_type().get_c_name()}x;
{indent}if ({self.get_parse_expr()}) {{
{generate_condition_check(self, indent + "    ") if self.condition else ""}
{self.generate_insert_code(qualified_c_name, indent + "    ")}{f"\n{indent}    {run_on_success}" if run_on_success else ""}
{indent}    return;
{indent}}}"""

    def generate_vala_decl(self, qualified_c_name: str, indent: str):
        vala_type = self.get_vala_type(qualified_c_name=qualified_c_name)
        vala_name = qualified_c_name.replace(".", "_")
        return f'{indent}[CCode(cname = "{qualified_c_name}")]\n{indent}public {vala_type}{vala_name};'

    def get_type_signature(self):
        '''Get a text representation of this type.'''
        return type(self).__name__

    def generate_insert_code(self, c_key: str, indent: str):
        '''Transfer data from the x variable to the config struct.'''
        return f"{indent}conf->{c_key} = x;"

    def require(self, condition: str):
        '''Check a condition before accepting a value. The value is available through a variable called x.'''
        self.condition = condition
        return self

class variant(BaseType):
    options: list[BaseType]
    def __init__(self, *options: list[BaseType]):
        super()
        self.options = options

    def __or__(self, other):
        self.options.append(other)
        return self

    def _get_option_enum(self, qualified_name: str, option: BaseType):
        return f"CONFIG_{constantify_type_signature(qualified_name)}_{constantify_type_signature(option.get_type_signature())}"

    def get_declaration_type(self, qualified_name: str):
        pascal_name = snake_case_to_pascal(qualified_name.replace("-", "_").replace(".", "_"))
        value_struct = DeclarationVariantStruct(pascal_name, [], {})

        for option in self.options:
            type_sig = option.get_type_signature()
            if not isinstance(option, enum):
                value_struct.props[f"v_{type_sig}"] = option.get_declaration_type(pascal_name + type_sig.capitalize())
            value_struct.options.append(constantify_type_signature(type_sig))

        return value_struct

    def get_parse_expr(self):
        raise TypeError("Variant types do not support parse expressions")

    def generate_insert_code(self):
        raise TypeError("Variant types do not support directly generating insert code")

    def generate_vala_decl(self, qualified_c_name, indent):
        return None

    def require(self):
        raise TypeError("Variant types do not support conditions, add checks on their members instead")

    def generate_parse_code(self, qualified_c_name, indent):
        parts = []
        no_union_members = all(option.generate_c_type() is None for option in self.options)
        for option in self.options:
            parts.append(f"""{indent}{{
{option.generate_parse_code(
    qualified_c_name + ".v_" + option.get_type_signature(),
    indent + "    ",
    run_on_success=f"conf->{qualified_c_name}{"" if no_union_members else ".type"} = {self._get_option_enum(qualified_c_name, option)};"
)}
{indent}}}""")

        return "\n".join(parts)

    def get_type_signature(self):
        return " | ".join(option.get_type_signature() for option in self.options)


class enum(BaseType):
    '''An enumeration value. Only meaningful in a variant.'''
    name: str
    def __init__(self, name: str):
        super()
        self.name = name

    def get_declaration_type(self, qualified_name):
        raise TypeError("Enum types do not support generating a C type")

    def get_parse_expr(self):
        raise TypeError("Enum types do not support parse expressions")

    def generate_insert_code(self, c_key, indent):
        raise TypeError("Enum types do not support directly generating insert code")

    def require(self, condition):
        raise TypeError("Enum types do not support conditions")

    def generate_parse_code(self, qualified_c_name: str, indent: str, run_on_success: str | None = None):
        return f"""{indent}if (strcmp(value, "{self.name}") == 0) {{
{indent}    {run_on_success}
{indent}    return;
{indent}}}"""

    def get_type_signature(self):
        return f"'{self.name}'"

class string(BaseType):
    def get_declaration_type(self, qualified_name):
        return DeclarationSimpleType("char *", "string ")

    def get_parse_expr(self):
        return "x = value, true"

    def generate_insert_code(self, c_key: str, indent: str):
        return f'''{indent}free(conf->{c_key});
{indent}conf->{c_key} = strdup(x);''';

class bool(BaseType):
    def get_declaration_type(self, qualified_name):
        return DeclarationSimpleType("bool ")

    def get_parse_expr(self):
        return "config_parse_bool(&x, value)"

class int(BaseType):
    def get_declaration_type(self, qualified_name):
        return DeclarationSimpleType("int ")

    def get_parse_expr(self):
        return "config_parse_int(&x, value)"

class color(BaseType):
    def get_declaration_type(self, qualified_name):
        return DeclarationStruct("Color", {
            "r": DeclarationSimpleType("float "),
            "g": DeclarationSimpleType("float "),
            "b": DeclarationSimpleType("float "),
            "a": DeclarationSimpleType("float "),
        }, "A 4-float color with straight alpha.")

    def get_parse_expr(self):
        return "config_parse_color(&x, value)"

class length(BaseType):
    def get_declaration_type(self, qualified_name):
        length_unit = DeclarationEnum("LengthUnit", ["PX"])
        return DeclarationStruct("Length", { "value": DeclarationSimpleType("double "), "unit": length_unit })

    def get_parse_expr(self):
        return "config_parse_length(&x, value)"

def config(props: dict[str, BaseType | dict[str, BaseType]]):
    indent = "    "

    vapi_parts = [
'''
[CCode(cheader_filename = "config.h", lower_case_cprefix = "config_", cprefix = "Config")]
namespace SpaceshotConfig {
    public enum LengthUnit {
        PX
    }

    public struct Length {
        double value;
        LengthUnit unit;
    }

    public struct Color {
        float r;
        float g;
        float b;
        float a;
    }

    [CCode(destroy_function = "", cname = "Config")]
    [Compact]
    public class Config {'''
    ]

    definition_parts = [
'''#include <spaceshot-config-struct-decl.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// This file is automatically generated by the Python scripts in the config/ directory. Do not edit manually.

// this is defined in parse.c
extern void config_warn(const char *format, ...);

// type parser functions
static bool config_parse_bool(bool *x, char *value) {
    if (strcmp(value, "true") == 0) {
        *x = true;
        return true;
    } else if (strcmp(value, "false") == 0) {
        *x = false;
        return true;
    } else {
        return false;
    }
}

static bool config_parse_int(int *x, char *value) {
    char *endptr;
    errno = 0;
    *x = strtol(value, &endptr, 0);
    if (value[0] == '\\0' || *endptr != '\\0' || errno) {
        return false;
    }
    return true;
}

static inline int parse_hex_digit(char digit) {
    return isdigit(digit) ? digit - '0' : tolower(digit) - 'a' + 10;
}

static inline float parse_hex_color_channel(char *text) {
    return (parse_hex_digit(text[0]) << 4 | parse_hex_digit(text[1])) / 255.0f;
}

static bool config_parse_color(ConfigColor *x, char *value) {
    if (value[0] == '#') {
        size_t len = strlen(value);
        // rest must be hex digits
        for (int i = 1; value[i] != '\\0'; i++) {
            char c = tolower(value[i]);
            if (c < '0' || c > 'f') {
                return false;
            }
        }

        if (len == 7) {
            // no alpha
            x->r = parse_hex_color_channel(value + 1);
            x->g = parse_hex_color_channel(value + 3);
            x->b = parse_hex_color_channel(value + 5);
            x->a = 1.0f;
            return true;
        } else if (len == 9) {
            // yes alpha
            x->r = parse_hex_color_channel(value + 1);
            x->g = parse_hex_color_channel(value + 3);
            x->b = parse_hex_color_channel(value + 5);
            x->a = parse_hex_color_channel(value + 7);
            return true;
        }
        return false;
    }
    // TODO: Add support for CSS rgb() notation
    return false;
}

static bool config_parse_length(ConfigLength *x, char *value) {
    char unit[3];
    int read_char_count;
    if (sscanf(value, "%5lf%2[a-z]%n", &x->value, unit, &read_char_count) != 2 || (size_t)read_char_count != strlen(value)) {
        return false;
    }

    if (strcmp(unit, "px") == 0) {
        x->unit = CONFIG_LENGTH_UNIT_PX;
    } else {
        return false;
    }

    return true;
}

void config_parse_entry(void *data, const char *section, const char *key, char *value) {
    Config *conf = data;

'''
    ]

    def traverse_section(ctx: DeclarationContext, props: dict[str, BaseType | dict[str, BaseType]], name: str | None) -> DeclarationStruct:
        struct = DeclarationStruct(snake_case_to_pascal(name or ""), {})

        for key, type in props.items():
            c_name = key.replace("-", "_")
            qualified_name = f"{name}.{c_name}" if name else c_name

            if isinstance(type, dict):
                assert name is None
                struct.props[key] = traverse_section(ctx, type, key)
            else:
                struct.props[key] = type.get_declaration_type(qualified_name)

        ctx.add(struct)
        return struct

    ctx = DeclarationContext()
    traverse_section(ctx, props, name=None)

#     vapi_parts.append('''
#     }

#     unowned Config get();
#     [CCode(array_length = false, array_null_terminated = true)]
#     unowned string[] get_locations();
#     void load_file(string path);
#     void load();
# }
# ''')

    # print(ctx.declarations)

    declaration_parts = [
'''#pragma once
// This file is automatically generated by the Python scripts in the config/ directory. Do not edit manually.
// IWYU pragma: private; include <config/config.h>
''',
    ]
    def generate_declarations(decls: list[DeclarationType], generated_names: set[str]):
        for decl in decls:
            c_name = decl.get_c_name()
            if c_name in generated_names:
                continue

            generated_names.add(c_name)
            generate_declarations(decl.get_dependent_types(), generated_names)
            decl_c = decl.generate_c()
            if decl_c:
                declaration_parts.append(decl_c)
    generate_declarations(ctx.declarations, set())

    config_h = str.join("\n", declaration_parts)
    print(config_h)

    config_c = str.join("\n", definition_parts)
    config_vapi = str.join("\n", vapi_parts)

    return config_c, config_h, config_vapi


# stuff used for definition generation:
# definition_parts.append(f'''{indent}if (strcmp(key, "{key}") == 0) {{
# {value.generate_parse_code(qualified_c_name, indent + "    ")}
# {indent}    config_warn("invalid value %s for key %s (needs to be {value.get_type_signature()})", value, key);
# {indent}    return;
# {indent}}}''')

# definition_parts.append(f"{indent}if (section == NULL) {{")

# definition_parts.append(f"{indent}}}")

# definition_parts[-1] += f' else if (strcmp(section, "{sec_name}") == 0) {{'

# definition_parts.append(f"{indent}}}")

# definition_parts.append(f'''
# {indent}if (section) {{
# {indent}    config_warn("unknown key [%s] %s", section, key);
# {indent}}} else {{
# {indent}    config_warn("unknown key %s", key);
# {indent}}}''')
# definition_parts.append("}\n")
