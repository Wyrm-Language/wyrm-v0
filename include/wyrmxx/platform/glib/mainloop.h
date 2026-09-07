#ifndef WYRMXX_GLIB_MAINLOOP_H_
#define WYRMXX_GLIB_MAINLOOP_H_

#include <wyrmxx/allocator.h>
#include <wyrmxx/main_loop.h>
#include <wyrmxx/except.h>
#include <wyrm/platform/glib/mainloop.h>

namespace wyrmxx
{
    class glib_mainloop : public main_loop
    {
    public:
        glib_mainloop(const glib_mainloop&) = delete;
        glib_mainloop& operator=(const glib_mainloop&) = delete;

        glib_mainloop(wy_allocator* alloc)
            : main_loop{wy_glib_mainloop_new(alloc)}
        {
            if (!self_) { throw out_of_memory{}; }
        }

        ~glib_mainloop() noexcept override
        {
            release();
        }

        glib_mainloop(glib_mainloop&& other) noexcept
            : main_loop{nullptr}
        {
            *this = std::move(other);
        }

        glib_mainloop& operator=(glib_mainloop&& other) noexcept
        {
            std::swap(self_, other.self_);
            return *this;
        }

        wy_uword get_active_sources() const
        {
            return wy_glib_mainloop_get_active_sources(self_);
        }

        void release() noexcept
        {
            if (self_) {
                wy_glib_mainloop_destroy(self_);
                self_ = nullptr;
            }
        }
    };
}

#endif
