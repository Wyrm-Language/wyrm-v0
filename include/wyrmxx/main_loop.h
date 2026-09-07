#ifndef WYRMXX_MAIN_LOOP_H_
#define WYRMXX_MAIN_LOOP_H_

#include <wyrm.h>
#include <wyrmxx/except.h>

namespace wyrmxx
{
    class main_loop
    {
    public:
        explicit main_loop(wy_main_loop* self) : self_{self} {}
        virtual ~main_loop() noexcept = default;

        operator wy_main_loop*() & { return self_; }
        wy_main_loop* ptr() { return self_; }

        wy_primitive add_fd(wy_handle fd, wy_io_condition events, wy_priority priority, wy_source_handle_cb cb, wy_primitive ud)
        {
            wy_primitive out{};
            check_wy_error(wy_main_loop_add_fd(*this, &out, fd, events, priority, cb, ud));
            return out;
        }

        wy_primitive add_timer(uint32_t ms, wy_priority priority, wy_source_cb cb, wy_primitive ud)
        {
            wy_primitive out{};
            check_wy_error(wy_main_loop_add_timer(*this, &out, ms, priority, cb, ud));
            return out;
        }

        wy_primitive add_idle(wy_source_cb cb, wy_primitive ud)
        {
            wy_primitive out{};
            check_wy_error(wy_main_loop_add_idle(*this, &out, cb, ud));
            return out;
        }

        wy_primitive add_wakeable(wy_priority priority, wy_source_cb cb, wy_primitive ud)
        {
            wy_primitive out{};
            check_wy_error(wy_main_loop_add_wakeable(*this, &out, priority, cb, ud));
            return out;
        }

        void trigger(wy_primitive src)
        {
            check_wy_error(wy_main_loop_trigger(*this, src));
        }

        void remove(wy_primitive src)
        {
            check_wy_error(wy_main_loop_remove(*this, src));
        }

        void iterate(bool may_block)
        {
            check_wy_error(wy_main_loop_iterate(*this, may_block));
        }

        void run()
        {
            check_wy_error(wy_main_loop_run(*this));
        }

        void quit()
        {
            check_wy_error(wy_main_loop_quit(*this));
        }

    protected:
        wy_main_loop* self_;
    };
}

#endif
