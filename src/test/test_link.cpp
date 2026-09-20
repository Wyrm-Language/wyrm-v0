#include <doctest/doctest.h>

#include <iterator>
#include <algorithm>
#include <cstring>
#include <string>
#include <wyrm/error.h>
#include <wyrm/string.h>
#include <wyrm/function.h>

#include <wyrm.h>
#include <wyrm/allocator.h>
#include <wyrm/builtins.h>
#include <wyrm/link.h>
#include <wyrm/module.h>
#include <wyrm/opcode.h>
#include <wyrm/platform/hosted/import_fs.h>
#include <wyrm/value.h>
#include <test_common/test_fiber_fixture.h>

#include "../wyrm/embedded/builtins.h"

namespace {

constexpr wy_u32 enc1(wy_u8 op, wy_u8 f, wy_u16 a0)
{
    return (wy_u32(a0) << 16) | (wy_u32(f) << 8) | wy_u32(op);
}

/** A wy_module with `nglobals` Unset globals and empty fill bookkeeping, ready for a free_names entry to be added. */
wy_module* make_module_with_globals(wy_context* ctx, wy_uword nglobals)
{
    wy_module* module = wy_module_new_f(ctx);
    module->global_count = nglobals;
    if (nglobals > 0) {
        module->globals = (wy_value*) wy_context_gc_alloc(ctx, sizeof(wy_value) * nglobals);
        module->fill_layer = (wy_u8*) wy_context_gc_alloc(ctx, sizeof(wy_u8) * nglobals);
        module->fill_source = (wy_symbol*) wy_context_gc_alloc(ctx, sizeof(wy_symbol) * nglobals);
        for (wy_uword i = 0; i < nglobals; i++) {
            module->globals[i] = wy_value_unset();
            module->fill_layer[i] = 0;
            module->fill_source[i] = WY_NULL;
        }
    }
    return module;
}

} // namespace

TEST_SUITE("link") {
    TEST_CASE("layer 3 fill matches a free name against a builtins export") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        wy_module* builtins = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(context, &builtins), WY_ERR_NONE);

        wy_module* module = make_module_with_globals(context, 1);
        wy_allocator* allocator = wy_context_get_machine(context)->allocator;
        REQUIRE_EQ(wy_slot_dict_expand_f(&module->free_names, allocator, 4), WY_ERR_NONE);

        wy_symbol println_sym;
        REQUIRE_EQ(wy_context_intern(context, "println", 7, &println_sym), WY_ERR_NONE);
        REQUIRE_EQ(wy_slot_dict_add_entry(&module->free_names, println_sym, 0), WY_ERR_NONE);

        REQUIRE_EQ(wy_link_fill_from_builtins(context, module, builtins), WY_ERR_NONE);

        CHECK_EQ(module->fill_layer[0], 3);
        wy_uword builtins_slot = wy_slot_dict_get(&builtins->exports, println_sym);
        REQUIRE_NE(builtins_slot, WY_SLOT_INVALID);
        // filled with the exact same callable builtins exports (correct + callable: a NATIVE object)
        CHECK_EQ(module->globals[0].type, WY_TYPE_TAG_NATIVE);
        CHECK_EQ(module->globals[0].data.gc_object, builtins->globals[builtins_slot].data.gc_object);
    }

    TEST_CASE("a free name builtins doesn't supply stays Unset after the fill") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        wy_module* builtins = WY_NULL;
        REQUIRE_EQ(wy_builtins_new(context, &builtins), WY_ERR_NONE);

        wy_module* module = make_module_with_globals(context, 1);
        wy_allocator* allocator = wy_context_get_machine(context)->allocator;
        REQUIRE_EQ(wy_slot_dict_expand_f(&module->free_names, allocator, 4), WY_ERR_NONE);

        wy_symbol missing_sym;
        REQUIRE_EQ(wy_context_intern(context, "does_not_exist", 15, &missing_sym), WY_ERR_NONE);
        REQUIRE_EQ(wy_slot_dict_add_entry(&module->free_names, missing_sym, 0), WY_ERR_NONE);

        REQUIRE_EQ(wy_link_fill_from_builtins(context, module, builtins), WY_ERR_NONE);

        CHECK_EQ(module->fill_layer[0], 0);
        CHECK(wy_value_is_unset(module->globals[0]));
    }

    TEST_CASE("wy_module_run_init on a trivial init runs it and marks the module ready") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        const wy_u32 code[] = { enc1(WY_OP_RETURN, 0, 0) };
        wy_module* module = make_module_with_globals(context, 0);
        module->code = code;
        module->code_len = std::size(code);
        module->init_nlocals = 0;

        CHECK_EQ(wy_module_run_init(context, module), WY_ERR_NONE);
        CHECK_EQ(module->state, WY_MODULE_READY);
    }

    TEST_CASE("wy_module_run_init on trapping init code faults and marks the module failed") {
        test_fiber_fixture ctx;
        wy_context* context = ctx.get_context_ptr();

        const wy_u32 code[] = { enc1(WY_OP_TRAP, 0, 0) };
        wy_module* module = make_module_with_globals(context, 0);
        module->code = code;
        module->code_len = std::size(code);
        module->init_nlocals = 0;

        CHECK_EQ(wy_module_run_init(context, module), WY_ERR_FAULT);
        CHECK_EQ(module->state, WY_MODULE_FAILED);
        CHECK(wy_value_is_error(context->current_fiber->fault));
    }
}

namespace {
wy_symbol symbol(wy_context* ctx, const char* text)
{
    wy_symbol out;
    REQUIRE_EQ(wy_context_intern(ctx, text, std::strlen(text), &out), WY_ERR_NONE);
    return out;
}

void name_slot(wy_context* ctx, wy_slot_dict* dict, const char* name, wy_uword slot)
{
    if (dict->capacity == 0) { REQUIRE_EQ(wy_slot_dict_expand_f(dict, ctx->parent->allocator, 32), WY_ERR_NONE); }
    REQUIRE_EQ(wy_slot_dict_add_entry(dict, symbol(ctx, name), slot), WY_ERR_NONE);
}

void import_static(wy_context* ctx, wy_module* mod, const char* name)
{
    mod->statics = static_cast<wy_value*>(wy_context_gc_alloc(ctx, sizeof(wy_value)));
    mod->static_count = 1;
    wy_string* text;
    REQUIRE_EQ(wy_string_strdup(ctx, name, &text), WY_ERR_NONE);
    mod->statics[0] = wy_value_object(WY_TYPE_TAG_STR, reinterpret_cast<wy_object*>(text));
}

wy_module* named_module(wy_context* ctx, const char* name, wy_uword globals,
    const wy_u32* code, wy_uword words)
{
    auto* mod = make_module_with_globals(ctx, globals);
    mod->name = symbol(ctx, name);
    mod->code = code;
    mod->code_len = words;
    mod->init_nlocals = 4;
    REQUIRE_EQ(wy_context_module_register(ctx, mod, nullptr), WY_ERR_NONE);
    return mod;
}
}

TEST_SUITE("link") {
    TEST_CASE("fill precedence and deferred ambiguity preserve same-source no-op") {
        test_fiber_fixture fix;
        auto* ctx = fix.get_context_ptr();
        auto* mod = make_module_with_globals(ctx, 1);
        REQUIRE_EQ(wy_context_set_root(ctx, mod), WY_ERR_NONE);
        name_slot(ctx, &mod->free_names, "item", 0);
        auto a = symbol(ctx, "a");
        auto b = symbol(ctx, "b");
        auto c = symbol(ctx, "c");
        REQUIRE_EQ(wy_link_fill(ctx, mod, 0, wy_value_word(3), 3, c), WY_ERR_NONE);
        REQUIRE_EQ(wy_link_fill(ctx, mod, 0, wy_value_word(2), 2, a), WY_ERR_NONE);
        REQUIRE_EQ(wy_link_fill(ctx, mod, 0, wy_value_word(99), 2, a), WY_ERR_NONE);
        CHECK_EQ(mod->globals[0].data.word, 2);
        REQUIRE_EQ(wy_link_fill(ctx, mod, 0, wy_value_word(2), 2, b), WY_ERR_NONE);
        CHECK_EQ(mod->fill_layer[0], 2);
        REQUIRE_EQ(wy_link_fill(ctx, mod, 0, wy_value_word(4), 2, b), WY_ERR_NONE);
        CHECK((mod->fill_layer[0] & WY_LINK_AMBIGUOUS) != 0);
        auto marker = mod->globals[0].data.gc_object;
        REQUIRE_EQ(wy_link_fill(ctx, mod, 0, wy_value_word(7), 2, a), WY_ERR_NONE);
        REQUIRE_EQ(wy_link_fill(ctx, mod, 0, wy_value_word(7), 2, c), WY_ERR_NONE);
        CHECK_EQ(mod->globals[0].data.gc_object, marker);
        wy_context_gc_full_run(ctx);
        auto* error = reinterpret_cast<wy_error_obj*>(marker);
        CHECK_EQ(error->code, WY_ERR_AMBIGUOUS);
        CHECK(std::string(error->what->str, error->what->len).find("'a' and 'b'") != std::string::npos);
        const wy_u32 code[] = {enc1(WY_OP_GGET, 0, 0), enc1(WY_OP_RETURN, 0, 0)};
        mod->code = code;
        mod->code_len = std::size(code);
        mod->init_nlocals = 1;
        CHECK_EQ(wy_module_run_init(ctx, mod), WY_ERR_FAULT);
        CHECK_EQ(ctx->current_fiber->fault.data.gc_object, marker);
        REQUIRE_EQ(wy_link_fill(ctx, mod, 0, wy_value_word(1), 1, c), WY_ERR_NONE);
        REQUIRE_EQ(wy_link_fill(ctx, mod, 0, wy_value_word(8), 2, b), WY_ERR_NONE);
        CHECK_EQ(mod->globals[0].data.word, 1);
        CHECK_EQ(mod->fill_layer[0], 1);
    }

    TEST_CASE("two_module synthetic import runs dependency inline once and fills qualified names") {
        test_fiber_fixture fix;
        auto* ctx = fix.get_context_ptr();
        ctx->gc_threshold = 0;
        const wy_u32 dep_code[] = {enc1(WY_OP_I8, 42, 0), enc1(WY_OP_GSET, 0, 0), enc1(WY_OP_RETURN, 0, 0)};
        const wy_u32 code[] = {enc1(WY_OP_IMPORT, 0, 0), enc1(WY_OP_IMPORT_WIDE, 0, 0), 1u << 16,
            enc1(WY_OP_GGET, 2, 0), enc1(WY_OP_GSET, 2, 1), enc1(WY_OP_RETURN, 0, 0)};
        auto* dep = named_module(ctx, "b", 1, dep_code, std::size(dep_code));
        name_slot(ctx, &dep->exports, "value", 0);
        auto* mod = named_module(ctx, "a", 2, code, std::size(code));
        import_static(ctx, mod, "b");
        name_slot(ctx, &mod->free_names, "b::value", 0);
        CHECK_EQ(wy_module_run_init(ctx, mod), WY_ERR_NONE);
        CHECK_EQ(dep->state, WY_MODULE_READY);
        CHECK_EQ(mod->globals[1].data.word, 42);
        dep->globals[0] = wy_value_word(99);
        auto* other = named_module(ctx, "other", 2, code, std::size(code));
        import_static(ctx, other, "b");
        name_slot(ctx, &other->free_names, "b::value", 0);
        CHECK_EQ(wy_module_run_init(ctx, other), WY_ERR_NONE);
        CHECK_EQ(other->globals[1].data.word, 99); // no second init
        CHECK_EQ(wy_module_run_init(ctx, dep), WY_ERR_NONE);
        CHECK_EQ(dep->globals[0].data.word, 99);
    }

    TEST_CASE("two_module cycle names both modules and fails every initialising importer") {
        test_fiber_fixture fix;
        auto* ctx = fix.get_context_ptr();
        const wy_u32 code[] = {enc1(WY_OP_IMPORT, 0, 0), enc1(WY_OP_RETURN, 0, 0)};
        auto* a = named_module(ctx, "a", 0, code, std::size(code));
        auto* b = named_module(ctx, "b", 0, code, std::size(code));
        import_static(ctx, a, "b");
        import_static(ctx, b, "a");
        CHECK_EQ(wy_module_run_init(ctx, a), WY_ERR_FAULT);
        CHECK_EQ(a->state, WY_MODULE_FAILED);
        CHECK_EQ(b->state, WY_MODULE_FAILED);
        auto* error = reinterpret_cast<wy_error_obj*>(ctx->current_fiber->fault.data.gc_object);
        REQUIRE_NE(error, nullptr);
        CHECK_EQ(error->code, WY_ERR_CYCLE);
        CHECK(std::string(error->what->str, error->what->len).find("'b' -> 'a'") != std::string::npos);
        CHECK_EQ(wy_module_run_init(ctx, b), WY_ERR_LINK);
    }

    TEST_CASE("wildcard inline init copies excepts and exports only") {
        test_fiber_fixture fix;
        auto* ctx = fix.get_context_ptr();
        ctx->gc_threshold = 0;
        const wy_u32 dep_code[] = {enc1(WY_OP_I8, 42, 0), enc1(WY_OP_GSET, 0, 0), enc1(WY_OP_RETURN, 0, 0)};
        const wy_u32 code[] = {enc1(WY_OP_LSYM, 0, 0), enc1(WY_OP_IMPORT_STAR, 1, 0), 0,
            enc1(WY_OP_LNIL, 0, 0), enc1(WY_OP_GGET, 1, 0), enc1(WY_OP_RETURN, 0, 0)};
        auto* dep = named_module(ctx, "b", 3, dep_code, std::size(dep_code));
        name_slot(ctx, &dep->exports, "value", 0);
        name_slot(ctx, &dep->exports, "excluded", 1);
        name_slot(ctx, &dep->free_names, "hidden", 2);
        dep->globals[1] = wy_value_word(10);
        dep->globals[2] = wy_value_word(20);
        auto* mod = named_module(ctx, "a", 3, code, std::size(code));
        import_static(ctx, mod, "b");
        name_slot(ctx, &mod->free_names, "value", 0);
        name_slot(ctx, &mod->free_names, "excluded", 1);
        name_slot(ctx, &mod->free_names, "hidden", 2);
        mod->symbols = static_cast<wy_symbol*>(wy_context_gc_alloc(ctx, sizeof(wy_symbol)));
        mod->symbol_count = 1;
        mod->symbols[0] = symbol(ctx, "excluded");
        REQUIRE_EQ(wy_module_run_init(ctx, mod), WY_ERR_NONE);
        CHECK_EQ(mod->globals[0].data.word, 42);
        CHECK(wy_value_is_unset(mod->globals[1]));
        CHECK(wy_value_is_unset(mod->globals[2]));
        REQUIRE_EQ(mod->wildcard_count, 1);
        CHECK_EQ(mod->wildcards[0].excepts[0], mod->symbols[0]);
    }
}

TEST_SUITE("link") {
    TEST_CASE("scope opcodes write owner bindings for modules classes and functions") {
        test_fiber_fixture fix;
        auto* ctx = fix.get_context_ptr();
        const wy_u32 empty[] = {enc1(WY_OP_RETURN, 0, 0)};
        auto* owner = named_module(ctx, "owner", 2, empty, std::size(empty));
        name_slot(ctx, &owner->exports, "value", 0);
        owner->globals[0] = wy_value_word(5);
        owner->globals[1] = wy_value_word(6);
        wy_class* cls;
        REQUIRE_EQ(wy_class_new(ctx, &cls), WY_ERR_NONE);
        cls->module = owner;
        name_slot(ctx, &cls->statics, "value", 1);
        wy_function* fn;
        REQUIRE_EQ(wy_function_new(ctx, owner, &owner->init_proto, nullptr, 0, &fn), WY_ERR_NONE);
        const wy_value receivers[] = {
            wy_value_object(WY_TYPE_TAG_MODULE, reinterpret_cast<wy_object*>(owner)),
            wy_value_object(WY_TYPE_TAG_CLASS, reinterpret_cast<wy_object*>(cls)),
            wy_value_object(WY_TYPE_TAG_FUNCTION, reinterpret_cast<wy_object*>(fn))
        };
        // lconst L0; i8 L1=77; setscope L0::value=L1; getscope L2=L0::value; gset g0=L2
        const wy_u32 code[] = {enc1(WY_OP_LCONST, 0, 0), enc1(WY_OP_I8, 77, 1),
            enc1(WY_OP_SETSCOPE, 0, 0), 1,
            enc1(WY_OP_GETSCOPE, 0, 2), 0,
            enc1(WY_OP_GSET, 2, 0), enc1(WY_OP_RETURN, 0, 0)};
        for (auto receiver : receivers) {
            auto* mod = named_module(ctx, "reader", 1, code, std::size(code));
            mod->statics = static_cast<wy_value*>(wy_context_gc_alloc(ctx, sizeof(wy_value)));
            mod->static_count = 1;
            mod->statics[0] = receiver;
            mod->symbols = static_cast<wy_symbol*>(wy_context_gc_alloc(ctx, sizeof(wy_symbol)));
            mod->symbol_count = 1;
            mod->symbols[0] = symbol(ctx, "value");
            REQUIRE_EQ(wy_module_run_init(ctx, mod), WY_ERR_NONE);
            CHECK_EQ(mod->globals[0].data.word, 77);
        }
        CHECK_EQ(owner->globals[0].data.word, 77);
        CHECK_EQ(owner->globals[1].data.word, 77);
        wy_value* binding = nullptr;
        CHECK_EQ(wy_link_scope_member(receivers[0], symbol(ctx, "missing"), &binding), WY_ERR_UNBOUND);
        wy_instance* instance;
        REQUIRE_EQ(wy_instance_new_f(ctx, cls, &instance), WY_ERR_NONE);
        CHECK_EQ(wy_link_scope_member(wy_value_object(WY_TYPE_TAG_INSTANCE,
            reinterpret_cast<wy_object*>(instance)), symbol(ctx, "value"), &binding), WY_ERR_BAD_TYPE);
        CHECK_EQ(wy_link_scope_member(wy_value_word(2), symbol(ctx, "value"), &binding), WY_ERR_BAD_TYPE);
    }

    TEST_CASE("explicit fill walks nested scopes and respects path component boundaries") {
        test_fiber_fixture fix;
        auto* ctx = fix.get_context_ptr();
        const wy_u32 code[] = {enc1(WY_OP_RETURN, 0, 0)};
        auto* dep = named_module(ctx, "b", 1, code, std::size(code));
        auto* nested = named_module(ctx, "nested", 1, code, std::size(code));
        name_slot(ctx, &dep->exports, "nested", 0);
        name_slot(ctx, &nested->exports, "value", 0);
        nested->globals[0] = wy_value_word(42);
        dep->globals[0] = wy_value_object(WY_TYPE_TAG_MODULE, reinterpret_cast<wy_object*>(nested));
        auto* mod = make_module_with_globals(ctx, 4);
        name_slot(ctx, &mod->free_names, "b", 0);
        name_slot(ctx, &mod->free_names, "b::nested::value", 1);
        name_slot(ctx, &mod->free_names, "bad::nested", 2);
        name_slot(ctx, &mod->free_names, "b::missing", 3);
        REQUIRE_EQ(wy_link_fill_from_import(ctx, mod, dep->name, dep), WY_ERR_NONE);
        CHECK_EQ(mod->globals[0].data.gc_object, reinterpret_cast<wy_object*>(dep));
        CHECK_EQ(mod->globals[1].data.word, 42);
        CHECK(wy_value_is_unset(mod->globals[2]));
        CHECK(wy_value_is_unset(mod->globals[3]));
    }

    TEST_CASE("wildcard identity is distinct from equal string contents and layer order is independent") {
        test_fiber_fixture fix;
        auto* ctx = fix.get_context_ptr();
        const wy_u32 empty[] = {enc1(WY_OP_RETURN, 0, 0)};
        auto* a = named_module(ctx, "a", 1, empty, std::size(empty));
        auto* b = named_module(ctx, "b", 1, empty, std::size(empty));
        name_slot(ctx, &a->exports, "item", 0);
        name_slot(ctx, &b->exports, "item", 0);
        wy_string* first;
        wy_string* second;
        REQUIRE_EQ(wy_string_strdup(ctx, "same", &first), WY_ERR_NONE);
        REQUIRE_EQ(wy_string_strdup(ctx, "same", &second), WY_ERR_NONE);
        a->globals[0] = wy_value_object(WY_TYPE_TAG_STR, reinterpret_cast<wy_object*>(first));
        b->globals[0] = a->globals[0];
        wy_wildcard wa{a, 0, nullptr}, wb{b, 0, nullptr};
        auto* mod = make_module_with_globals(ctx, 1);
        name_slot(ctx, &mod->free_names, "item", 0);
        REQUIRE_EQ(wy_link_fill_from_wildcard(ctx, mod, &wa), WY_ERR_NONE);
        REQUIRE_EQ(wy_link_fill_from_wildcard(ctx, mod, &wb), WY_ERR_NONE);
        CHECK_EQ(mod->fill_layer[0], 2);
        b->globals[0] = wy_value_object(WY_TYPE_TAG_STR, reinterpret_cast<wy_object*>(second));
        REQUIRE_EQ(wy_link_fill_from_wildcard(ctx, mod, &wb), WY_ERR_NONE);
        CHECK((mod->fill_layer[0] & WY_LINK_AMBIGUOUS) != 0);
        // Every ordering of three layers must leave layer 1's value.
        int order[] = {1, 2, 3};
        do {
            mod->fill_layer[0] = 0;
            for (int layer : order) {
                REQUIRE_EQ(wy_link_fill(ctx, mod, 0, wy_value_word(layer), static_cast<wy_u8>(layer), a->name), WY_ERR_NONE);
            }
            CHECK_EQ(mod->globals[0].data.word, 1);
        } while (std::next_permutation(std::begin(order), std::end(order)));
    }

    TEST_CASE("failed dependency init unwinds and subsequent imports reject the failed cache entry") {
        test_fiber_fixture fix;
        auto* ctx = fix.get_context_ptr();
        const wy_u32 trap[] = {enc1(WY_OP_TRAP, 0, 0)};
        const wy_u32 code[] = {enc1(WY_OP_IMPORT_STAR, 0, 0), 0, enc1(WY_OP_RETURN, 0, 0)};
        auto* dep = named_module(ctx, "b", 0, trap, std::size(trap));
        auto* a = named_module(ctx, "a", 0, code, std::size(code));
        import_static(ctx, a, "b");
        auto* frame = ctx->current_fiber->current_frame;
        auto* top = ctx->current_fiber->value_stack.top;
        REQUIRE_EQ(wy_module_run_init(ctx, a), WY_ERR_FAULT);
        CHECK_EQ(dep->state, WY_MODULE_FAILED);
        CHECK_EQ(ctx->current_fiber->current_frame, frame);
        CHECK_EQ(ctx->current_fiber->value_stack.top, top);
        wy_module* found = nullptr;
        CHECK_EQ(wy_link_import(ctx, a->statics[0].data.str, &found), WY_ERR_LINK);
    }

    TEST_CASE("import rejects invalid paths missing hooks and malformed hook images") {
        test_fiber_fixture fix;
        auto* ctx = fix.get_context_ptr();
        wy_module* found = nullptr;
        for (const char* name : {"", "::a", "a::", "a:::b", "a:b"}) {
            wy_string* path;
            REQUIRE_EQ(wy_string_strdup(ctx, name, &path), WY_ERR_NONE);
            CHECK_EQ(wy_link_import(ctx, path, &found), WY_ERR_INVAL);
        }
        wy_string* path;
        REQUIRE_EQ(wy_string_strdup(ctx, "missing", &path), WY_ERR_NONE);
        CHECK_EQ(wy_link_import(ctx, path, &found), WY_ERR_UNBOUND);
        ctx->import_hook = [](wy_context* context, const char*, wy_uword, wy_u8** bytes, wy_uword* len, const wy_module_image**, void*) -> wy_error {
            *len = 4;
            *bytes = static_cast<wy_u8*>(wy_context_gc_alloc(context, *len));
            std::memset(*bytes, 0, *len);
            return WY_ERR_NONE;
        };
        CHECK_EQ(wy_link_import(ctx, path, &found), WY_ERR_IMAGE);
        CHECK_EQ(ctx->module_count, 0);
    }
}

TEST_SUITE("link") {
    TEST_CASE("nested imports exhaust the fiber stack and unwind without leaving modules initialising") {
        test_fiber_fixture fix;
        auto* ctx = fix.get_context_ptr();
        const wy_u32 code[] = {enc1(WY_OP_IMPORT, 0, 0), enc1(WY_OP_RETURN, 0, 0)};
        wy_module* modules[4];
        for (unsigned i = 0; i < 4; i++) {
            std::string name = "depth" + std::to_string(i);
            modules[i] = named_module(ctx, name.c_str(), 0, code, std::size(code));
            modules[i]->init_nlocals = 400;
            std::string next = "depth" + std::to_string(i + 1);
            import_static(ctx, modules[i], next.c_str());
        }
        REQUIRE_EQ(wy_module_run_init(ctx, modules[0]), WY_ERR_FAULT);
        CHECK_EQ(modules[0]->state, WY_MODULE_FAILED);
        CHECK_EQ(modules[1]->state, WY_MODULE_FAILED);
        CHECK_EQ(modules[2]->state, WY_MODULE_FAILED);
        CHECK_EQ(modules[3]->state, WY_MODULE_LOADED);
    }
}

TEST_SUITE("builtin_table") {
    // Epic 11 M1: the embedded compiler + library images, consulted by the
    // hosted import hook after every -I root misses (src/wyrm/embedded/).

    static wy_import_fs_search_path table_search_path()
    {
        wy_import_fs_search_path search;
        wy_import_fs_search_path_init_s(&search);
        search.builtins = wyrm_builtin_modules;
        search.builtin_count = wyrm_builtin_module_count;
        return search;
    }

    TEST_CASE("wyrm::compiler::module resolves from the builtin table with no filesystem") {
        test_fiber_fixture fix;
        auto* ctx = fix.get_context_ptr();
        wy_import_fs_search_path search = table_search_path();  // no roots at all
        ctx->import_hook = wy_import_fs_hook;
        ctx->import_ud = &search;

        wy_module* module = nullptr;
        wy_string* path;
        REQUIRE_EQ(wy_string_strdup(ctx, "wyrm::compiler::module", &path), WY_ERR_NONE);
        REQUIRE_EQ(wy_link_import(ctx, path, &module), WY_ERR_NONE);
        REQUIRE_NE(module, nullptr);
        // The real compiler module, not a stub: a five-word fixture would
        // not compile anything.
        CHECK_GT(module->code_len, 1000u);
        CHECK_EQ(ctx->module_count, 1u);

        // The expander the std::expand child VM loads by name.
        wy_module* expander = nullptr;
        wy_string* expand_path;
        REQUIRE_EQ(wy_string_strdup(ctx, "wyrm::compiler::expand", &expand_path), WY_ERR_NONE);
        CHECK_EQ(wy_link_import(ctx, expand_path, &expander), WY_ERR_NONE);

        // Package-relative spellings (image.wy imports `bjson::*`) resolve too.
        wy_module* alias = nullptr;
        wy_string* alias_path;
        REQUIRE_EQ(wy_string_strdup(ctx, "bjson", &alias_path), WY_ERR_NONE);
        CHECK_EQ(wy_link_import(ctx, alias_path, &alias), WY_ERR_NONE);
        CHECK_NE(alias, module);
    }

    TEST_CASE("a same-named module in a -I root shadows the builtin table") {
        test_fiber_fixture fix;
        auto* ctx = fix.get_context_ptr();
        wy_import_fs_search_path search = table_search_path();
        std::string shadow = std::string(WY_TEST_BYTECODE_DIR) + "/embedded/shadow";
        REQUIRE_EQ(wy_import_fs_add_root(wy_context_get_machine(ctx)->allocator,
            &search, shadow.c_str()), WY_ERR_NONE);
        ctx->import_hook = wy_import_fs_hook;
        ctx->import_ud = &search;

        wy_module* module = nullptr;
        wy_string* path;
        REQUIRE_EQ(wy_string_strdup(ctx, "wyrm::compiler::module", &path), WY_ERR_NONE);
        REQUIRE_EQ(wy_link_import(ctx, path, &module), WY_ERR_NONE);
        REQUIRE_NE(module, nullptr);
        // The shadow fixture is `x := 1` compiled by pypoc - a handful of
        // words, not the embedded compiler module's thousand-plus.
        CHECK_LT(module->code_len, 100u);
    }

    TEST_CASE("a module in neither a root nor the table stays WY_ERR_UNBOUND") {
        test_fiber_fixture fix;
        auto* ctx = fix.get_context_ptr();
        wy_import_fs_search_path search = table_search_path();
        ctx->import_hook = wy_import_fs_hook;
        ctx->import_ud = &search;

        wy_module* module = nullptr;
        wy_string* path;
        REQUIRE_EQ(wy_string_strdup(ctx, "no::such::module", &path), WY_ERR_NONE);
        CHECK_EQ(wy_link_import(ctx, path, &module), WY_ERR_UNBOUND);
    }
}
