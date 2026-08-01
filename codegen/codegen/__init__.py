import cog
import toml
from pydantic import BaseModel, Field

_CPP_FN_TEMPLATE = """
{storage_class} {return_type} {fn_name}({fn_args})
{{
{fn_body}
}}
"""


class FunctionArgument(BaseModel):
    argtype: str
    name: str | None = None
    reg: str | None = None  # Register receiving the argument.


class MemberFunction(BaseModel):
    name: str
    ret: str = "void"
    # If set, the generated function allocates return-value storage and passes
    # its pointer in the specified register or stack position.
    retbuf: str | int | None = None
    access: str = "public"
    callconv: str = "__cdecl"
    stacksize: int | None = None  # Required when the caller cleans the stack.
    static: bool = False
    # Whether offset names a global pointer exposed through a property accessor.
    prop: bool = False
    offset: int  # Relative to the game-module base address.
    doc: str | None = None
    this: str | None = None  # Register receiving `this`, if any.
    # Arguments in signature order. Stack arguments are pushed in reverse order;
    # register-bound arguments generally come first.
    args: list[FunctionArgument] = Field(default_factory=list)


class Function(BaseModel):
    name: str
    ret: str
    retbuf: str | int | None = None
    callconv: str = "__cdecl"
    stacksize: int | None = None
    offset: int
    doc: str | None = None
    args: list[FunctionArgument] = Field(default_factory=list)


def create_getter_cpp_expr(return_type: str, addr_expr="_addr", static: bool = False) -> str:
    if static:
        return f"return *(reinterpret_cast<{return_type}*>({addr_expr}));\n"
    else:
        raise NotImplementedError


def create_property_fn_cpp_expr(fn: MemberFunction):
    if len(fn.args) == 0:
        return create_getter_cpp_expr(fn.ret, "_addr", static=fn.static)
    elif len(fn.args) == 1:
        raise NotImplementedError
    else:
        raise RuntimeError


def create_member_fn_cpp_expr(fn: MemberFunction):
    asm_calls = []
    stack_exprs = []
    cpp_header = ""
    cpp_footer = ""

    if fn.retbuf is not None:
        cpp_header += fn.ret + " ret_val;\n"
        cpp_header += "auto ret_val_ptr = &ret_val;\n"
        # Emit a register-bound hidden return-buffer argument first.
        if isinstance(fn.retbuf, str):
            asm_calls.append(f"mov {fn.retbuf}, ret_val_ptr")

    # Handle a stack return buffer in the first position.
    if isinstance(fn.retbuf, int) and fn.retbuf == 1:
        stack_exprs.append("ret_val_ptr")

    if not fn.static:
        if fn.this is not None:
            asm_calls.append(f"mov {fn.this}, this")
        else:
            stack_exprs.append("this")

    for i, arg in enumerate(fn.args):
        if arg.reg is not None:
            asm_calls.append(f"mov {arg.reg}, {arg.name}")
        else:
            arg_name = arg.name or f"arg_{i}"
            if isinstance(fn.retbuf, int) and fn.retbuf == len(stack_exprs) + 1:
                stack_exprs.append("rel_val_ptr")
            stack_exprs.append(arg_name)

    # Handle a stack return buffer after the explicit arguments.
    if isinstance(fn.retbuf, int) and fn.retbuf > len(stack_exprs):
        stack_exprs.append("ret_val_ptr")

    # Push stack arguments right to left.
    stack_exprs.reverse()
    for expr in stack_exprs:
        asm_calls.append(f"push {expr}")

    asm_calls.append("call _addr")
    if fn.callconv == "__cdecl" and fn.stacksize is not None and fn.stacksize > 0:
        asm_calls.append(f"add esp,{fn.stacksize}")

    asm_expr = " ".join(map(lambda x: f"__asm {x}", asm_calls)) + "\n"
    if fn.retbuf is not None:
        cpp_footer += "return ret_val;\n"

    return cpp_header + asm_expr + cpp_footer


def member_fn_to_cpp(fn: MemberFunction) -> str:
    addr = hex(fn.offset)
    fn_body = f"const uintptr_t _addr = memory::Map::base_addr + {addr};\n"

    if fn.prop:
        fn_body += create_property_fn_cpp_expr(fn)
    else:
        fn_body += create_member_fn_cpp_expr(fn)

    return _CPP_FN_TEMPLATE.format(
        storage_class="static" if fn.static else "",
        return_type=fn.ret,
        fn_name=fn.name,
        fn_args=",".join(f"{a.argtype} {a.name or f'arg_{i}'}" for i, a in enumerate(fn.args)),
        fn_body=fn_body.strip(),
    ).strip()


def fn_to_cpp(fn: Function) -> str:
    asm_calls = []
    stack_exprs = []
    cpp_header = ""
    cpp_footer = ""

    if fn.retbuf is not None:
        cpp_header += fn.ret + " ret_val;\n"
        cpp_header += "auto ret_val_ptr = &ret_val;\n"
        # Emit a register-bound hidden return-buffer argument first.
        if isinstance(fn.retbuf, str):
            asm_calls.append(f"mov {fn.retbuf}, ret_val_ptr")

    # Handle a stack return buffer in the first position.
    if isinstance(fn.retbuf, int) and fn.retbuf == 1:
        stack_exprs.append("ret_val_ptr")

    for i, arg in enumerate(fn.args):
        if arg.reg is not None:
            asm_calls.append(f"mov {arg.reg}, {arg.name}")
        else:
            arg_name = arg.name or f"arg_{i}"
            if isinstance(fn.retbuf, int) and fn.retbuf == len(stack_exprs) + 1:
                stack_exprs.append("rel_val_ptr")
            stack_exprs.append(arg_name)

    # Handle a stack return buffer after the explicit arguments.
    if isinstance(fn.retbuf, int) and fn.retbuf > len(stack_exprs):
        stack_exprs.append("ret_val_ptr")

    # Push stack arguments right to left.
    stack_exprs.reverse()
    for expr in stack_exprs:
        asm_calls.append(f"push {expr}")

    asm_calls.append("call _addr")
    if fn.callconv == "__cdecl" and fn.stacksize is not None and fn.stacksize > 0:
        asm_calls.append(f"add esp,{fn.stacksize}")

    asm_expr = " ".join(map(lambda x: f"__asm {x}", asm_calls)) + "\n"
    if fn.retbuf is not None:
        cpp_footer += "return ret_val;\n"

    fn_body = f"const uintptr_t _addr = memory::Map::base_addr + {fn.offset};\n" + cpp_header + asm_expr + cpp_footer
    return _CPP_FN_TEMPLATE.format(
        storage_class="",
        return_type=fn.ret,
        fn_name=fn.name,
        fn_args=",".join(f"{a.argtype} {a.name or f'arg_{i}'}" for i, a in enumerate(fn.args)),
        fn_body=fn_body,
    ).strip()


def print_class_model_fns(filename: str, *, access: str | None = None) -> None:
    with open(filename, "r", encoding="utf-8") as fd:
        data = toml.load(fd)

    fns = list(filter(lambda x: access is None or x.access == access, map(MemberFunction.parse_obj, data["fns"])))
    for fn in fns:
        cog.outl(member_fn_to_cpp(fn))


def print_fn_file(filename: str) -> None:
    with open(filename, "r", encoding="utf-8") as fd:
        data = toml.load(fd)

    fns = map(Function.parse_obj, data["fns"])
    for fn in fns:
        cog.outl(fn_to_cpp(fn))
