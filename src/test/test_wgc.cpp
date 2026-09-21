#include <doctest/doctest.h>

#include <wyrm/box.h>
#include <wyrm/pair.h>
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

        wy_gc_collect_start_f(fix.context, arena);
        wy_gc_object_visit(fix.context, &outer->object);
        wy_gc_collect_finish_f(fix.context, arena);

        REQUIRE(arena_contains(arena, &outer->object));
        REQUIRE(arena_contains(arena, &inner->object));
        REQUIRE_FALSE(arena_contains(arena, &dead->object));
        (void) arena_check_count(arena);
    }

    TEST_CASE("mark walks a 10k-deep pair chain without recursion") {
        test_context_fixture fix;
        wy_context* context = fix.get_context_ptr();
        wy_gc_arena* arena = arena_of(fix);

        const wy_uword chain_len = 10000;
        wy_pair* head = wy_pair_new_f(context);
        REQUIRE_NE(head, WY_NULL);
        wy_pair* tail = head;
        for (wy_uword i = 1; i < chain_len; i++) {
            wy_pair* next = wy_pair_new_f(context);
            REQUIRE_NE(next, WY_NULL);
            tail->cdr = wy_value_object(WY_TYPE_TAG_PAIR, &next->object);
            tail = next;
        }

        /* head is the only externally-held reference; everything else is
         * reachable only by walking cdr chains, so a recursive visit at
         * this depth would blow the C stack if one were still used. */
        wy_gc_collect_start_f(context, arena);
        wy_gc_object_visit(context, &head->object);
        wy_gc_collect_finish_f(context, arena);

        REQUIRE(arena_contains(arena, &head->object));
        REQUIRE(arena_contains(arena, &tail->object));
        REQUIRE_FALSE(context->gc_abandoned);
    }

    TEST_CASE("gc_threshold = 0 collects at every safepoint and existing objects survive it") {
        test_context_fixture fix;
        wy_context* context = fix.get_context_ptr();
        wy_gc_arena* arena = arena_of(fix);

        context->gc_threshold = 0; context->gc_growth_factor = 0;

        wy_pair* rooted = wy_pair_new_f(context);
        REQUIRE_NE(rooted, WY_NULL);
        wy_value rooted_value = wy_value_object(WY_TYPE_TAG_PAIR, &rooted->object);
        REQUIRE_EQ(wy_context_root_push_f(context, &rooted_value), WY_ERR_NONE);

        /* Every allocation below crosses the (zero) threshold; the rooted
         * pair must still be alive afterward. */
        for (int i = 0; i < 50; i++) {
            wy_box* scratch = WY_NULL;
            REQUIRE_EQ(wy_box_new_f(context, &scratch), WY_ERR_NONE);
            wy_context_gc_safepoint(context);
        }

        REQUIRE(arena_contains(arena, &rooted->object));
        wy_context_root_pop_f(context);
    }

    TEST_CASE("root push/pop protects an object across a forced collection") {
        test_context_fixture fix;
        wy_context* context = fix.get_context_ptr();
        wy_gc_arena* arena = arena_of(fix);

        wy_box* protected_box = WY_NULL;
        REQUIRE_EQ(wy_box_new_f(context, &protected_box), WY_ERR_NONE);
        wy_value protected_value = box_value(protected_box);

        wy_box* unrooted = WY_NULL;
        REQUIRE_EQ(wy_box_new_f(context, &unrooted), WY_ERR_NONE);
        const wy_object* unrooted_obj = &unrooted->object;

        REQUIRE_EQ(wy_context_root_push_f(context, &protected_value), WY_ERR_NONE);
        wy_context_gc_full_run(context);

        REQUIRE(arena_contains(arena, &protected_box->object));
        REQUIRE_FALSE(arena_contains(arena, unrooted_obj));

        wy_context_root_pop_f(context);
        wy_context_gc_full_run(context);
        REQUIRE_FALSE(arena_contains(arena, &protected_box->object));
    }
}
