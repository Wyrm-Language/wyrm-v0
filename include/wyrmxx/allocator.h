#ifndef WYRMXX_ALLOCATOR_H_
#define WYRMXX_ALLOCATOR_H_

#include <wyrm.h>
#include <wyrmxx/except.h>

namespace wyrmxx
{
    class allocator final
    {
    public:
        explicit allocator(wyrm_allocator* alloc) noexcept
            : self_{alloc}
        {
        }

        operator wyrm_allocator*() const { return self_; }

        template<typename T>
        T* alloc()
        {
            auto new_ptr = static_cast<T*>(wyrm_allocator_alloc(self_, sizeof(T)));
            if (!new_ptr) { throw out_of_memory{}; }
            return new_ptr;
        }

        template<typename T>
        void free(T* ptr)
        {
            wyrm_allocator_free(self_, static_cast<void*>(ptr));
        }

    private:
        wyrm_allocator* self_;
    };

}

#endif
