#ifndef WYRMXX_MAIN_LOOP_H_
#define WYRMXX_MAIN_LOOP_H_

#include <wyrm.h>
#include <wyrm/internal_api.h>

#include <wyrmxx/except.h>



namespace wyrmxx
{
    class main_loop
    {
    public:
        explicit main_loop(wyrm_main_loop* self) : self_{self} {}
        virtual ~main_loop() noexcept = default;

        operator wyrm_main_loop*() & { return self_; }
        wyrm_main_loop* ptr() { return self_; }

        wyrm_primitive add_fd(wyrm_handle fd, wyrm_io_condition events, wyrm_priority priority, wyrm_source_handle_cb cb, wyrm_primitive ud)
        {
            wyrm_primitive out{};
            check_wyrm_error(wyrm_main_loop_add_fd(*this, &out, fd, events, priority, cb, ud));
            return out;
        }

        wyrm_primitive add_timer(uint32_t ms, wyrm_priority priority, wyrm_source_cb cb, wyrm_primitive ud)
        {
            wyrm_primitive out{};
            check_wyrm_error(wyrm_main_loop_add_timer(*this, &out, ms, priority, cb, ud));
            return out;
        }

        wyrm_primitive add_idle(wyrm_source_cb cb, wyrm_primitive ud)
        {
            wyrm_primitive out{};
            check_wyrm_error(wyrm_main_loop_add_idle(*this, &out, cb, ud));
            return out;
        }

        wyrm_primitive add_wakeable(wyrm_priority priority, wyrm_source_cb cb, wyrm_primitive ud)
        {
            wyrm_primitive out{};
            check_wyrm_error(wyrm_main_loop_add_wakeable(*this, &out, priority, cb, ud));
            return out;
        }

        void trigger(wyrm_primitive src)
        {
            check_wyrm_error(wyrm_main_loop_trigger(*this, src));
        }

        void remove(wyrm_primitive src)
        {
            check_wyrm_error(wyrm_main_loop_remove(*this, src));
        }

        void iterate(bool may_block)
        {
            check_wyrm_error(wyrm_main_loop_iterate(*this, may_block));
        }

        void run()
        {
            check_wyrm_error(wyrm_main_loop_run(*this));
        }

        void quit()
        {
            check_wyrm_error(wyrm_main_loop_quit(*this));
        }

    protected:
        wyrm_main_loop* self_;
    };
}

#endif
