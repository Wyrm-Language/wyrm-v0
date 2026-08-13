# Wyrm Standard Library

## Builtin Functions

  - len(x): Return number of elements within a collection

## Builtin Primitives

### str

Messages:

  - substr(begin: int, end: int) -> str: Return substring start offset begin and ending at offset before end.

## Multiprocessing and Messaging

### External Context

The 'thread' builtin accepts a module path as an argument. The module is then
run as an external thread. The function returns a remote namespace:

    context = thread(myprogram::thread)

The thread object:

    class Thread:
        slot id: thread_id

    # Join the thread
    fn [Thread] join(): ...



### Signal

The Signal class:

    class Signal:
        ...

    fn [Signal] emit(obj):
        ...

    fn [Signal] connect(callable):
        ...

The transmitted object _must_ be serializable.
