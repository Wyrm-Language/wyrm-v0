#include <doctest/doctest.h>

#include <wyrm/box.h>
#include <wyrmxx/core.h>
#include <test_common/test_context_fixture.h>

namespace {

wy_gc_arena* arena_of(test_context_fixture& fix)
{
    return &fix.get_context_ptr()->arena;
}

bool arena_contains(wy_gc_arena* arena, const wy_object* object)
{
    for (wy_object* cur = arena->first; cur; cur = cur->next) {
        if (cur == object) { return true; }
    }
    return false;
}

wy_uword arena_check_count(wy_gc_arena* arena)
{
    wy_uword count = 0;
    wy_object* last = WY_NULL;
    for (wy_object* cur = arena->first; cur; cur = cur->next) {
        last = cur;
        ++count;
    }
    REQUIRE_EQ(arena->last, last);
    if (arena->last) {
        REQUIRE_EQ(arena->last->next, WY_NULL);
    }
    return count;
}

wy_value box_value(wy_box* box)
{
    wy_value v = {};
    v.type = WY_TYPE_TAG_BOX;
    v.data.gc_object = &box->object;
    return v;
}

} // namespace

TEST_SUITE("wgc") {
    TEST_CASE("observe tracked objects") {
        test_context_fixture fix;
        wy_gc_arena* arena = arena_of(fix);

        const wy_uword before = arena_check_count(arena);

        wy_box* box = WY_NULL;
        REQUIRE_EQ(wy_box_new_f(fix.context, &box), WY_ERR_NONE);

        REQUIRE_EQ(arena_check_count(arena), before + 1);
        REQUIRE(arena_contains(arena, &box->object));
    }

    TEST_CASE("free untracked object") {
        test_context_fixture fix;
        wy_gc_arena* arena = arena_of(fix);
        const wy_uword before = arena_check_count(arena);

        wy_box* box = WY_NULL;
        REQUIRE_EQ(wy_box_new_f(fix.context, &box), WY_ERR_NONE);
        const wy_object* dead = &box->object;

        wy_gc_collect_start_f(fix.context, arena);
        wy_gc_collect_finish_f(fix.context, arena);

        REQUIRE_FALSE(arena_contains(arena, dead));
        /* list invariants must survive the sweep */
        REQUIRE_EQ(before, arena_check_count(arena));
    }

    TEST_CASE("sweep frees and and maintains list") {
        test_context_fixture fix;
        wy_gc_arena* arena = arena_of(fix);

        wy_box* outer = WY_NULL;
        wy_box* dead = WY_NULL;
        wy_box* inner = WY_NULL;
        REQUIRE_EQ(wy_box_new_f(fix.context, &outer), WY_ERR_NONE);
        REQUIRE_EQ(wy_box_new_f(fix.context, &dead), WY_ERR_NONE);
        REQUIRE_EQ(wy_box_new_f(fix.context, &inner), WY_ERR_NONE);

        wy_box_set_value_f(outer, box_value(inner));

        wy_state state{};
        wy_state_init_from_context_f(&state, fix.context);

        wy_gc_collect_start_f(fix.context, arena);
        wy_gc_object_visit(&state, &outer->object);
        wy_gc_collect_finish_f(fix.context, arena);

        REQUIRE(arena_contains(arena, &outer->object));
        REQUIRE(arena_contains(arena, &inner->object));
        REQUIRE_FALSE(arena_contains(arena, &dead->object));
        (void) arena_check_count(arena);
    }
}
