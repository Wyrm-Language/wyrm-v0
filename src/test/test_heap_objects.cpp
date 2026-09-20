#include <doctest/doctest.h>

#include <wyrm.h>
#include <wyrm/bytes.h>
#include <wyrm/error.h>
#include <wyrm/function.h>
#include <wyrm/list.h>
#include <wyrm/module.h>
#include <wyrm/native.h>
#include <wyrm/string.h>
#include <wyrm/tuple.h>
#include <wyrmxx/core.h>

#include <test_common/test_fiber_fixture.h>

namespace {

bool arena_contains(wy_context* context, const wy_object* object)
{
    for (wy_object* cur = context->arena.first; cur; cur = cur->next) {
        if (cur == object) { return true; }
    }
    return false;
}

wy_error make_str(wy_context* context, const char* text, wy_string** out)
{
    return wy_string_strdup(context, text, out);
}

wy_error leaf_stub(wy_context*, wy_value*, wy_uword, wy_value*, wy_uword)
{
    return WY_ERR_NONE;
}

wy_exec_state exec_stub(wy_context*, wy_primitive)
{
    return WY_EXEC_DONE;
}

} // namespace


TEST_SUITE("heap_objects") {

    TEST_CASE("tuple: fields and children survive a rooted GC pass") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        wy_string* str = WY_NULL;
        REQUIRE_EQ(make_str(ctx, "tuple-child", &str), WY_ERR_NONE);

        wy_value items[2] = {
            wy_value_object(WY_TYPE_TAG_STR, (wy_object*) str),
            wy_value_nil(),
        };

        wy_tuple* tup = WY_NULL;
        REQUIRE_EQ(wy_tuple_new(ctx, items, 2, &tup), WY_ERR_NONE);
        REQUIRE_NE(tup, WY_NULL);
        REQUIRE_EQ(tup->count, 2u);
        REQUIRE(tup->items[0] == items[0]);
        REQUIRE(tup->items[1] == items[1]);

        REQUIRE_EQ(wy_context_push(ctx, wy_value_object(WY_TYPE_TAG_TUPLE, (wy_object*) tup)), WY_ERR_NONE);
        fix.run_gc();

        REQUIRE(arena_contains(ctx, (wy_object*) tup));
        REQUIRE(arena_contains(ctx, (wy_object*) str));
    }

    TEST_CASE("tuple: unrooted children are collected") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        wy_string* str = WY_NULL;
        REQUIRE_EQ(make_str(ctx, "orphan", &str), WY_ERR_NONE);

        wy_value items[1] = { wy_value_object(WY_TYPE_TAG_STR, (wy_object*) str) };
        wy_tuple* tup = WY_NULL;
        REQUIRE_EQ(wy_tuple_new(ctx, items, 1, &tup), WY_ERR_NONE);

        const wy_object* dead_tup = (wy_object*) tup;
        const wy_object* dead_str = (wy_object*) str;

        fix.run_gc();

        REQUIRE_FALSE(arena_contains(ctx, dead_tup));
        REQUIRE_FALSE(arena_contains(ctx, dead_str));
    }

    TEST_CASE("list: push, at, set, grow") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        wy_list* list = WY_NULL;
        REQUIRE_EQ(wy_list_new(ctx, 0, &list), WY_ERR_NONE);
        REQUIRE_NE(list, WY_NULL);
        REQUIRE_EQ(list->count, 0u);

        for (wy_word i = 0; i < 10; ++i) {
            REQUIRE_EQ(wy_list_push(ctx, list, wy_value_word(i)), WY_ERR_NONE);
        }
        REQUIRE_EQ(list->count, 10u);
        REQUIRE(list->capacity >= 10u);

        REQUIRE_EQ(*wy_list_at_f(list, 3), wy_value_word(3));
        REQUIRE_EQ(wy_list_at_f(list, 100), WY_NULL);

        REQUIRE_EQ(wy_list_set(list, 3, wy_value_word(99)), WY_ERR_NONE);
        REQUIRE_EQ(*wy_list_at_f(list, 3), wy_value_word(99));
        REQUIRE_EQ(wy_list_set(list, 100, wy_value_word(0)), WY_ERR_RANGE);

        wy_string* str = WY_NULL;
        REQUIRE_EQ(make_str(ctx, "list-child", &str), WY_ERR_NONE);
        REQUIRE_EQ(wy_list_push(ctx, list, wy_value_object(WY_TYPE_TAG_STR, (wy_object*) str)), WY_ERR_NONE);

        REQUIRE_EQ(wy_context_push(ctx, wy_value_object(WY_TYPE_TAG_LIST, (wy_object*) list)), WY_ERR_NONE);
        fix.run_gc();

        REQUIRE(arena_contains(ctx, (wy_object*) list));
        REQUIRE(arena_contains(ctx, (wy_object*) str));
    }

    TEST_CASE("bytes: copies data, reserve grows, finalize doesn't crash") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        const wy_u8 payload[] = { 1, 2, 3, 4, 5 };
        wy_bytes* buf = WY_NULL;
        REQUIRE_EQ(wy_bytes_new(ctx, payload, sizeof(payload), &buf), WY_ERR_NONE);
        REQUIRE_NE(buf, WY_NULL);
        REQUIRE_EQ(buf->len, sizeof(payload));
        REQUIRE_NE(buf->data, WY_NULL);
        REQUIRE_NE(buf->data, payload);
        for (wy_uword i = 0; i < sizeof(payload); ++i) {
            REQUIRE_EQ(buf->data[i], payload[i]);
        }

        REQUIRE_EQ(wy_bytes_reserve(ctx, buf, 128), WY_ERR_NONE);
        REQUIRE(buf->capacity >= 128u);
        REQUIRE_EQ(buf->len, sizeof(payload));
        for (wy_uword i = 0; i < sizeof(payload); ++i) {
            REQUIRE_EQ(buf->data[i], payload[i]);
        }

        wy_bytes* empty = WY_NULL;
        REQUIRE_EQ(wy_bytes_new(ctx, WY_NULL, 0, &empty), WY_ERR_NONE);
        REQUIRE_EQ(empty->len, 0u);

        /* leave both unrooted; the fixture's dtor runs a full GC over them */
    }

    TEST_CASE("error_obj: fields and children (what, payload) survive GC") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        wy_string* what = WY_NULL;
        REQUIRE_EQ(make_str(ctx, "boom", &what), WY_ERR_NONE);

        wy_string* payload_str = WY_NULL;
        REQUIRE_EQ(make_str(ctx, "payload", &payload_str), WY_ERR_NONE);
        wy_value payload = wy_value_object(WY_TYPE_TAG_STR, (wy_object*) payload_str);

        wy_error_obj* err = WY_NULL;
        REQUIRE_EQ(wy_error_obj_new(ctx, WY_NULL, what, payload, &err), WY_ERR_NONE);
        REQUIRE_NE(err, WY_NULL);
        REQUIRE_EQ(err->cls, WY_NULL);
        REQUIRE_EQ(err->what, what);
        REQUIRE(err->payload == payload);

        REQUIRE_EQ(wy_context_push(ctx, wy_value_object(WY_TYPE_TAG_ERROR, (wy_object*) err)), WY_ERR_NONE);
        fix.run_gc();

        REQUIRE(arena_contains(ctx, (wy_object*) err));
        REQUIRE(arena_contains(ctx, (wy_object*) what));
        REQUIRE(arena_contains(ctx, (wy_object*) payload_str));
    }

    TEST_CASE("function: closes over module + captures, both survive GC") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        wy_module* module = wy_module_new_f(ctx);
        REQUIRE_NE(module, WY_NULL);

        wy_function_proto proto{};
        proto.nparams = 0;
        proto.ncaptures = 1;

        wy_string* cap_str = WY_NULL;
        REQUIRE_EQ(make_str(ctx, "captured", &cap_str), WY_ERR_NONE);
        wy_value caps[1] = { wy_value_object(WY_TYPE_TAG_STR, (wy_object*) cap_str) };

        wy_function* fn = WY_NULL;
        REQUIRE_EQ(wy_function_new(ctx, module, &proto, caps, 1, &fn), WY_ERR_NONE);
        REQUIRE_NE(fn, WY_NULL);
        REQUIRE_EQ(fn->module, module);
        REQUIRE_EQ(fn->proto, &proto);
        REQUIRE_EQ(fn->ncaps, 1u);
        REQUIRE(fn->caps[0] == caps[0]);

        REQUIRE_EQ(wy_context_push(ctx, wy_value_object(WY_TYPE_TAG_FUNCTION, (wy_object*) fn)), WY_ERR_NONE);
        fix.run_gc();

        REQUIRE(arena_contains(ctx, (wy_object*) fn));
        REQUIRE(arena_contains(ctx, (wy_object*) module));
        REQUIRE(arena_contains(ctx, (wy_object*) cap_str));
    }

    TEST_CASE("native: leaf and exec constructors") {
        test_fiber_fixture fix;
        wy_context* ctx = fix.get_context_ptr();

        wy_symbol name = WY_NULL;
        REQUIRE_EQ(wy_context_intern(ctx, "my-native", 9, &name), WY_ERR_NONE);

        wy_native* leaf = WY_NULL;
        REQUIRE_EQ(wy_native_leaf_new(ctx, name, 1, 3, leaf_stub, &leaf), WY_ERR_NONE);
        REQUIRE_NE(leaf, WY_NULL);
        REQUIRE_EQ(leaf->kind, (wy_u8) WY_NATIVE_LEAF);
        REQUIRE_EQ(leaf->name, name);
        REQUIRE_EQ(leaf->min_argc, 1);
        REQUIRE_EQ(leaf->max_argc, 3);
        REQUIRE_EQ(leaf->fn.leaf, leaf_stub);

        wy_native* exec = WY_NULL;
        REQUIRE_EQ(wy_native_exec_new(ctx, name, 0, 0, wy_exec_fn_create(exec_stub, wy_primitive_null()), &exec), WY_ERR_NONE);
        REQUIRE_NE(exec, WY_NULL);
        REQUIRE_EQ(exec->kind, (wy_u8) WY_NATIVE_EXEC);
        REQUIRE_EQ(exec->fn.exec.fn, exec_stub);

        /* natives hold no wy_value refs; just confirm they're GC-tracked */
        REQUIRE(arena_contains(ctx, (wy_object*) leaf));
        REQUIRE(arena_contains(ctx, (wy_object*) exec));
    }
}
