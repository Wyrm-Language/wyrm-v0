#include <doctest/doctest.h>

#include <wyrm/wbox.h>
#include <wyrmxx/wcore.h>
#include <test_common/test_context_fixture.h>

namespace {

wyrm_gc_arena* arena_of(test_context_fixture& fix)
{
    return &fix.get_context_ptr()->arena;
}

bool arena_contains(wyrm_gc_arena* arena, const wyrm_object* object)
{
    for (wyrm_object* cur = arena->first; cur; cur = cur->next) {
        if (cur == object) { return true; }
    }
    return false;
}

wyrm_uword arena_check_count(wyrm_gc_arena* arena)
{
    wyrm_uword count = 0;
    wyrm_object* last = WYRM_NULL;
    for (wyrm_object* cur = arena->first; cur; cur = cur->next) {
        last = cur;
        ++count;
    }
    REQUIRE_EQ(arena->last, last);
    REQUIRE_EQ(arena->last->next, WYRM_NULL);
    return count;
}

wyrm_value box_value(wyrm_box* box)
{
    wyrm_value v = {};
    v.type = WYRM_TYPE_TAG_BOX;
    v.data.gc_object = &box->object;
    return v;
}

} // namespace

TEST_SUITE("wgc") {
    TEST_CASE("observe tracked objects") {
        test_context_fixture fix;
        wyrm_gc_arena* arena = arena_of(fix);

        const wyrm_uword before = arena_check_count(arena);

        wyrm_box* box = WYRM_NULL;
        REQUIRE_EQ(wyrm_box_new_f(fix.context, &box), WYRM_ERR_NONE);

        REQUIRE_EQ(arena_check_count(arena), before + 1);
        REQUIRE(arena_contains(arena, &box->object));
    }

    TEST_CASE("free untracked object") {
        test_context_fixture fix;
        wyrm_gc_arena* arena = arena_of(fix);
        const wyrm_uword before = arena_check_count(arena);

        wyrm_box* box = WYRM_NULL;
        REQUIRE_EQ(wyrm_box_new_f(fix.context, &box), WYRM_ERR_NONE);
        const wyrm_object* dead = &box->object;

        wyrm_gc_collect_start_f(fix.context, arena);
        wyrm_gc_collect_finish_f(fix.context, arena);

        REQUIRE_FALSE(arena_contains(arena, dead));
        /* list invariants must survive the sweep */
        REQUIRE_EQ(before, arena_check_count(arena));
    }

    TEST_CASE("sweep frees and and maintains list") {
        test_context_fixture fix;
        wyrm_gc_arena* arena = arena_of(fix);

        wyrm_box* outer = WYRM_NULL;
        wyrm_box* dead = WYRM_NULL;
        wyrm_box* inner = WYRM_NULL;
        REQUIRE_EQ(wyrm_box_new_f(fix.context, &outer), WYRM_ERR_NONE);
        REQUIRE_EQ(wyrm_box_new_f(fix.context, &dead), WYRM_ERR_NONE);
        REQUIRE_EQ(wyrm_box_new_f(fix.context, &inner), WYRM_ERR_NONE);

        wyrm_box_set_value_f(outer, box_value(inner));

        wyrm_state state{};
        wyrm_state_init_from_context_f(&state, fix.context);

        wyrm_gc_collect_start_f(fix.context, arena);
        wyrm_gc_object_visit(&state, &outer->object);
        wyrm_gc_collect_finish_f(fix.context, arena);

        REQUIRE(arena_contains(arena, &outer->object));
        REQUIRE(arena_contains(arena, &inner->object));
        REQUIRE_FALSE(arena_contains(arena, &dead->object));
        (void) arena_check_count(arena);
    }
}
