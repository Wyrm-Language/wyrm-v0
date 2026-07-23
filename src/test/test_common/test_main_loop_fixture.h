#ifndef TEST_MAIN_LOOP_FIXTURE_H
#define TEST_MAIN_LOOP_FIXTURE_H

#include <algorithm>
#include <list>
#include <memory>
#include <variant>

#include <wyrm/internal_api.h>
#include <wyrmxx/main_loop.h>
#include <wyrmxx/context.h>

class test_main_loop_fixture_
{
public:
    struct ext_loop {
        wyrm_main_loop loop;
        test_main_loop_fixture_* self;
    };

    test_main_loop_fixture_()
        : loop_{ .loop = { .vt = &vt_ }, .self = this }
    {}

    wyrm_main_loop* ptr() { return &loop_.loop; }
    bool quit_requested() const { return quit_; }

    size_t num_sources() const { return sources_.size(); }

    void run_triggered()
    {
        for (auto iter = sources_.begin(); iter != sources_.end();) {
            auto& source_ptr = *iter;
            bool keep = true;
            if (!source_ptr)
            {
                keep = false;
            }
            else if (source_ptr->triggered)
            {
                keep = exec_source(*source_ptr);
            }
            /* else if (!source_ptr->triggered) {} */

            if (!keep)
                iter = sources_.erase(iter);
            else
                ++iter;
        }
    }

private:
    struct idle_source {
        wyrm_source_cb cb;
    };

    struct wakeable_source {
        wyrm_source_cb cb;
    };

    using source_data = std::variant<idle_source, wakeable_source>;

    struct source {
        source_data data;
        wyrm_primitive user_data;
        bool triggered;
    };

    static bool exec_source(source& source)
    {
        if (std::holds_alternative<idle_source>(source.data)) {
            auto& idle = std::get<idle_source>(source.data);
            source.triggered = false;
            return idle.cb(source.user_data);
        }
        else if (std::holds_alternative<wakeable_source>(source.data))
        {
            auto wakeable = std::get<wakeable_source>(source.data);
            source.triggered = false;
            return wakeable.cb(source.user_data);
        }
        /* Unimplemented / unknown source - should be unreached, remove */
        return false;
    }


    using source_list = std::list<std::unique_ptr<source>>;

    static test_main_loop_fixture_& get_self(wyrm_main_loop* self)
    {
        return *reinterpret_cast<ext_loop*>(self)->self;
    }

    static wyrm_error n_add_fd(wyrm_main_loop*, wyrm_primitive*, wyrm_handle, wyrm_io_condition, wyrm_priority, wyrm_source_handle_cb, wyrm_primitive)
    {
        return WYRM_ERR_NOSUPPORT;
    }

    static wyrm_error n_add_timer(wyrm_main_loop*, wyrm_primitive*, uint32_t, wyrm_priority, wyrm_source_cb, wyrm_primitive)
    {
        return WYRM_ERR_NOSUPPORT;
    }

    static wyrm_error n_add_idle(wyrm_main_loop* ref, wyrm_primitive* out, wyrm_source_cb cb, wyrm_primitive ud)
    {
        auto& thiz = get_self(ref);

        auto source_ptr = std::make_unique<source>(source{ idle_source{ cb }, ud, true });
        if (out) { *out = wyrm_primitive_ptr(source_ptr.get() ); }

        thiz.sources_.emplace_back(std::move(source_ptr));
        return WYRM_ERR_NONE;
    }

    static wyrm_error n_add_wakeable(wyrm_main_loop* ref, wyrm_primitive* out, wyrm_priority /*priority*/, wyrm_source_cb cb, wyrm_primitive ud)
    {
        auto& thiz = get_self(ref);

        auto source_ptr = std::make_unique<source>(source{ wakeable_source{ cb }, ud, false });
        if (out) { *out = wyrm_primitive_ptr(source_ptr.get() ); }

        thiz.sources_.emplace_back(std::move(source_ptr));
        return WYRM_ERR_NONE;
    }

    static wyrm_error n_trigger(wyrm_main_loop* /*ref*/, wyrm_primitive src)
    {
        auto* s = static_cast<source*>(src.ptr);
        if (!s) return WYRM_ERR_INVAL;
        if (!std::holds_alternative<wakeable_source>(s->data)) return WYRM_ERR_INVAL;
        s->triggered = true;
        return WYRM_ERR_NONE;
    }

    static wyrm_error n_remove(wyrm_main_loop* ref, wyrm_primitive src)
    {
        auto& thiz = get_self(ref);
        auto* s = static_cast<source*>(src.ptr);
        auto it = std::find_if(thiz.sources_.begin(), thiz.sources_.end(),
            [s](const auto& p) { return p.get() == s; });
        if (it == thiz.sources_.end()) return WYRM_ERR_INVAL;
        thiz.sources_.erase(it);
        return WYRM_ERR_NONE;
    }

    static wyrm_error n_iterate(wyrm_main_loop* ref, bool /*may_block*/)
    {
        auto& thiz = get_self(ref);
        thiz.run_triggered();
        return WYRM_ERR_NONE;
    }

    static wyrm_error n_run(wyrm_main_loop* /*ref*/)
    {
        return WYRM_ERR_NOSUPPORT;
    }

    static wyrm_error n_quit(wyrm_main_loop* ref)
    {
        get_self(ref).quit_ = true;
        return WYRM_ERR_NONE;
    }

    static inline wyrm_main_loop_vt vt_ = {
        .add_fd       = n_add_fd,
        .add_timer    = n_add_timer,
        .add_idle     = n_add_idle,
        .add_wakeable = n_add_wakeable,
        .trigger      = n_trigger,
        .remove       = n_remove,
        .iterate      = n_iterate,
        .run          = n_run,
        .quit         = n_quit,
    };

    ext_loop loop_;
    source_list sources_;
    bool quit_ = false;
};

class test_main_loop_fixture : public wyrmxx::main_loop
{
public:
    test_main_loop_fixture(const test_main_loop_fixture&) = delete;
    test_main_loop_fixture& operator=(const test_main_loop_fixture&) = delete;

    test_main_loop_fixture()
        : main_loop{nullptr}
    {
        self_ = p_.ptr();
    }

    main_loop get() { return main_loop(p_.ptr()); }

private:
    test_main_loop_fixture_ p_;
};

#endif
