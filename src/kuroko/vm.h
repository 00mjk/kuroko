#pragma once
/**
 * @file vm.h
 * @brief Core API for the bytecode virtual machine.
 *
 * Functions and structures declared here make up the bulk of the public C API
 * for Kuroko, including initializing the VM and passing code to be interpreted.
 */
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include "kuroko.h"
#include "value.h"
#include "table.h"
#include "object.h"

/**
 * @def KRK_CALL_FRAMES_MAX
 * @brief Maximum depth of the call stack in managed-code function calls.
 */
#define KRK_CALL_FRAMES_MAX 64

/**
 * @def KRK_THREAD_SCRATCH_SIZE
 * @brief Extra space for each thread to store a set of working values safe from the GC.
 *
 * Various operations require threads to remove values from the stack but ensure
 * they are not lost to garbage collection. This space allows each thread to keep
 * a few things around during those operations.
 */
#define KRK_THREAD_SCRATCH_SIZE 3

/**
 * @brief Represents a managed call state in a VM thread.
 *
 * For every managed function call, including the top-level module,
 * a call frame is added to the stack to track the running function,
 * the current opcode instruction, the offset into the stack, and
 * the valid globals table.
 *
 * Call frames are used directly by the VM as the source of
 * opcodes and operands during execution, and are used by the exception
 * handler to roll back execution to the appropriate environment.
 */
typedef struct {
	KrkClosure * closure; /**< Pointer to the function object containing the code object for this frame */
	uint8_t * ip;         /**< Instruction pointer within the code object's bytecode data */
	size_t slots;         /**< Offset into the stack at which this function call's arguments begin */
	size_t outSlots;      /**< Offset into the stack at which stackTop will be reset upon return */
	KrkTable * globals;   /**< Pointer to the attribute table containing valud global vairables for this call */
	struct timespec in_time;
} KrkCallFrame;

/**
 * @brief Table of basic exception types.
 *
 * These are the core exception types, available in managed code
 * from the builtin namespace. A single instance of this struct
 * is attached to the global VM state so that C code can quickly
 * access these exception types for use with krk_runtimeException.
 *
 * @see krk_runtimeException
 */
struct Exceptions {
	KrkClass * baseException;       /**< @exception Exception The base exception type. */
	KrkClass * typeError;           /**< @exception TypeError An argument or value was not of the expected type. */
	KrkClass * argumentError;       /**< @exception ArgumentException The number of arguments passed to a function was not as expected. */
	KrkClass * indexError;          /**< @exception IndexError An attempt was made to reference an invalid array index. */
	KrkClass * keyError;            /**< @exception KeyError An attempt was made to reference an invalid mapping key. */
	KrkClass * attributeError;      /**< @exception AttributeError An attempt was made to reference an invalid object property. */
	KrkClass * nameError;           /**< @exception NameError An attempt was made to reference an undeclared global variable. */
	KrkClass * importError;         /**< @exception ImportError An error was encountered when attempting to import a module. */
	KrkClass * ioError;             /**< @exception IOError An error was encountered in operating system's IO library. */
	KrkClass * valueError;          /**< @exception ValueError The value of a parameter or variable is not valid. */
	KrkClass * keyboardInterrupt;   /**< @exception KeyboardInterrupt An interrupt signal was received. */
	KrkClass * zeroDivisionError;   /**< @exception ZeroDivisionError A mathematical function attempted to divide by zero. */
	KrkClass * notImplementedError; /**< @exception NotImplementedError The method is not implemented, either for the given arguments or in general. */
	KrkClass * syntaxError;         /**< @exception SyntaxError The compiler encountered an unrecognized or invalid source code input. */
	KrkClass * assertionError;      /**< @exception AssertionError An @c assert statement failed. */
};

/**
 * @brief Table of classes for built-in object types.
 *
 * For use by C modules and within the VM, an instance of this struct
 * is attached to the global VM state. At VM initialization, each
 * built-in class is attached to this table, and the class values
 * stored here are used for integrated type checking with krk_isInstanceOf.
 *
 * @note As this and other tables are used directly by embedders, do not
 *       reorder the layout of the individual class pointers, even if
 *       it looks nicer. The ordering here is part of our library ABI.
 */
struct BaseClasses {
	KrkClass * objectClass;          /**< The base of all classes within the type tree. */
	KrkClass * moduleClass;          /**< A class for representing imported modules, both managed and C. */
	KrkClass * typeClass;            /**< Classes themselves are of this class. */
	KrkClass * intClass;             /**< Primitive integer type. */
	KrkClass * floatClass;           /**< Primitive double-precision floating-point type. */
	KrkClass * boolClass;            /**< Primitive boolean type. */
	KrkClass * noneTypeClass;        /**< The class of the None value. */
	KrkClass * strClass;             /**< Built-in Unicode string type. */
	KrkClass * functionClass;        /**< Represents a function object (KrkClosure) or native bind (KrkNative) */
	KrkClass * methodClass;          /**< Represents a bound method (KrkBoundMethod) */
	KrkClass * tupleClass;           /**< An immutable collection of arbitrary values. */
	KrkClass * bytesClass;           /**< An immutable sequence of bytes. */
	KrkClass * listiteratorClass;    /**< Iterator over lists */
	KrkClass * rangeClass;           /**< An object representing a start and end point for a sequence of integers. */
	KrkClass * rangeiteratorClass;   /**< Iterator over a range of values */
	KrkClass * striteratorClass;     /**< Iterator over characters (by codepoint) in a string */
	KrkClass * tupleiteratorClass;   /**< Iterator over values in a tuple */
	KrkClass * listClass;            /**< Mutable collection of arbitrary values. */
	KrkClass * dictClass;            /**< Mutable mapping of hashable keys to arbitrary values. */
	KrkClass * dictitemsClass;       /**< Iterator over the (key,value) pairs of a dict */
	KrkClass * dictkeysClass;        /**< Iterator over the keys of a dict */
	KrkClass * bytesiteratorClass;   /**< Iterator over the integer byte values of a bytes object. */
	KrkClass * propertyClass;        /**< Magic object that calls a function when accessed from an instance through the dot operator. */
	KrkClass * codeobjectClass;      /**< Static compiled bytecode container (KrkCodeObject) */
	KrkClass * generatorClass;       /**< Generator object. */
	KrkClass * notImplClass;         /**< NotImplementedType */
	KrkClass * bytearrayClass;       /**< Mutable array of bytes */
	KrkClass * dictvaluesClass;      /**< Iterator over values of a dict */
	KrkClass * sliceClass;           /**< Slice object */
};

/**
 * @brief Execution state of a VM thread.
 *
 * Each thread in the VM has its own local thread state, which contains
 * the thread's stack, stack pointer, call frame stack, a thread-specific
 * VM flags bitarray, and an exception state.
 *
 * @see krk_currentThread
 */
typedef struct KrkThreadState {
	struct KrkThreadState * next; /**< Invasive list pointer to next thread. */

	KrkCallFrame * frames;     /**< Call frame stack for this thread, max KRK_CALL_FRAMES_MAX */
	size_t frameCount;         /**< Number of active call frames. */
	size_t stackSize;          /**< Size of the allocated stack space for this thread. */
	KrkValue * stack;          /**< Pointer to the bottom of the stack for this thread. */
	KrkValue * stackTop;       /**< Pointer to the top of the stack. */
	KrkUpvalue * openUpvalues; /**< Flexible array of unclosed upvalues. */
	ssize_t exitOnFrame;       /**< When called in a nested context, the frame offset to exit the VM dispatch loop on. */

	KrkInstance * module;      /**< The current module execution context. */
	KrkValue currentException; /**< When an exception is thrown, it is stored here. */
	int flags;                 /**< Thread-local VM flags; each thread inherits the low byte of the global VM flags. */
	KrkValue * stackMax;       /**< End of allocated stack space. */

	KrkValue scratchSpace[KRK_THREAD_SCRATCH_SIZE]; /**< A place to store a few values to keep them from being prematurely GC'd. */
} KrkThreadState;

/**
 * @brief Global VM state.
 *
 * This state is shared by all VM threads and stores the
 * path to the VM binary, global execution flags, the
 * string and module tables, tables of builtin types,
 * and the state of the (shared) garbage collector.
 */
typedef struct KrkVM {
	int globalFlags;                  /**< Global VM state flags */
	char * binpath;                   /**< A string representing the name of the interpreter binary. */
	KrkTable strings;                 /**< Strings table */
	KrkTable modules;                 /**< Module cache */
	KrkInstance * builtins;           /**< '\__builtins__' module */
	KrkInstance * system;             /**< 'kuroko' module */
	KrkValue * specialMethodNames;    /**< Cached strings of important method and function names */
	struct BaseClasses * baseClasses; /**< Pointer to a (static) namespacing struct for the KrkClass*'s of built-in object types */
	struct Exceptions * exceptions;   /**< Pointer to a (static) namespacing struct for the KrkClass*'s of basic exception types */

	/* Garbage collector state */
	KrkObj * objects;                 /**< Linked list of all objects in the GC */
	size_t bytesAllocated;            /**< Running total of bytes allocated */
	size_t nextGC;                    /**< Point at which we should sweep again */
	size_t grayCount;                 /**< Count of objects marked by scan. */
	size_t grayCapacity;              /**< How many objects we can fit in the scan list. */
	KrkObj** grayStack;               /**< Scan list */

	KrkThreadState * threads;         /**< Invasive linked list of all VM threads. */
	FILE * callgrindFile;             /**< File to write unprocessed callgrind data to. */
	size_t maximumCallDepth;          /**< Maximum recursive call depth. */
} KrkVM;

/* Thread-specific flags */
#define KRK_THREAD_ENABLE_TRACING      (1 << 0)
#define KRK_THREAD_ENABLE_DISASSEMBLY  (1 << 1)
#define KRK_THREAD_ENABLE_SCAN_TRACING (1 << 2)
#define KRK_THREAD_HAS_EXCEPTION       (1 << 3)
#define KRK_THREAD_SINGLE_STEP         (1 << 4)
#define KRK_THREAD_SIGNALLED           (1 << 5)
#define KRK_THREAD_DEFER_STACK_FREE    (1 << 6)

/* Global flags */
#define KRK_GLOBAL_ENABLE_STRESS_GC    (1 << 8)
#define KRK_GLOBAL_GC_PAUSED           (1 << 9)
#define KRK_GLOBAL_CLEAN_OUTPUT        (1 << 10)
#define KRK_GLOBAL_CALLGRIND           (1 << 11)
#define KRK_GLOBAL_REPORT_GC_COLLECTS  (1 << 12)
#define KRK_GLOBAL_THREADS             (1 << 13)

#ifdef ENABLE_THREADING
#  define threadLocal __thread
#else
#  define threadLocal
#endif

/**
 * @brief Thread-local VM state.
 *
 * See @c KrkThreadState for more information.
 */
#if defined(ENABLE_THREADING) && ((defined(__APPLE__)) && defined(__aarch64__))
extern void krk_forceThreadData(void);
#define krk_currentThread (*_macos_currentThread())
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
inline KrkThreadState * _macos_currentThread(void) {
	extern const uint64_t tls_desc[] asm("_krk_currentThread");
	const uintptr_t * threadptr; asm ("mrs %0, TPIDRRO_EL0" : "=r"(threadptr));
	return (KrkThreadState*)(threadptr[tls_desc[1]] + tls_desc[2]);
}
#elif defined(ENABLE_THREADING) && ((defined(_WIN32) && !defined(KRKINLIB)) || defined(KRK_MEDIOCRE_TLS))
#define krk_currentThread (*krk_getCurrentThread())
#else
extern threadLocal KrkThreadState krk_currentThread;
#endif

/**
 * @brief Singleton instance of the shared VM state.
 */
extern KrkVM krk_vm;

/**
 * @def vm
 * @brief Convenience macro for namespacing.
 */
#define vm krk_vm

/**
 * @brief Initialize the VM at program startup.
 * @memberof KrkVM
 *
 * All library users must call this exactly once on startup to create
 * the built-in types, modules, and functions for the VM and prepare
 * the string and module tables. Optionally, callers may set `vm.binpath`
 * before calling krk_initVM to allow the VM to locate the interpreter
 * binary and establish the default module paths.
 *
 * @param flags Combination of global VM flags and initial thread flags.
 */
extern void krk_initVM(int flags);

/**
 * @brief Release resources from the VM.
 * @memberof KrkVM
 *
 * Generally, it is desirable to call this once before the hosting program exits.
 * If a fresh VM state is needed, krk_freeVM should be called before a further
 * call to krk_initVM is made. The resources released here can include allocated
 * heap memory, FILE pointers or descriptors, or various other things which were
 * initialized by C extension modules.
 */
extern void krk_freeVM(void);

/**
 * @brief Reset the current thread's stack state to the top level.
 *
 * In a repl, this should be called before or after each iteration to clean up any
 * remnant stack entries from an uncaught exception. It should not be called
 * during normal execution by C extensions. Values on the stack may be lost
 * to garbage collection after a call to @c krk_resetStack .
 */
extern void krk_resetStack(void);

/**
 * @brief Compile and execute a source code input.
 *
 * Compiles and executes the source code in @p src and returns the result
 * of execution - generally the return value of a function body or the
 * last value on the stack in a REPL expression. This is the lowest level
 * call for most usecases, including execution of commands from a REPL or
 * when executing a file.
 *
 * The string provided in @p fromFile is used in exception tracebacks.
 *
 * @param src      Source code to compile and run.
 * @param fromFile Path to the source file, or a representative string like "<stdin>".
 * @return The value of the executed code, which is either the value of an explicit 'return'
 *         statement, or the last expression value from an executed statement.  If an uncaught
 *         exception occurred, this will be @c None and @c krk_currentThread.flags should
 *         indicate @c KRK_THREAD_HAS_EXCEPTION and @c krk_currentThread.currentException
 *         should contain the raised exception value.
 */
extern KrkValue krk_interpret(const char * src, char * fromFile);

/**
 * @brief Load and run a source file and return when execution completes.
 *
 * Loads and runs a source file. Can be used by interpreters to run scripts,
 * either in the context of a new a module or as if they were continuations
 * of the current module state (eg. as if they were lines entered on a repl)
 *
 * @param fileName  Path to the source file to read and execute.
 * @param fromFile  Value to assign to @c \__file__
 * @return As with @c krk_interpret, an object representing the newly created module,
 *         or the final return value of the VM execution.
 */
extern KrkValue krk_runfile(const char * fileName, char * fromFile);

/**
 * @brief Load and run a file as a module.
 *
 * Similar to @c krk_runfile but ensures that execution of the VM returns to the caller
 * after the file is run. This should be run after calling @c krk_startModule to initialize
 * a new module context and is used internally by the import mechanism.
 *
 * @param fileName Path to the source file to read and execute.
 * @param fromFile Value to assign to @c \__file__
 * @return The object representing the module, or None if execution of the file failed.
 */
extern KrkValue krk_callfile(const char * fileName, char * fromFile);

/**
 * @brief Push a stack value.
 *
 * Pushes a value onto the current thread's stack, triggering a
 * stack resize if there is not enough space to hold the new value.
 *
 * @param value Value to push.
 */
extern void krk_push(KrkValue value);

/**
 * @brief Pop the top of the stack.
 *
 * Removes and returns the value at the top of current thread's stack.
 * Generally, it is preferably to leave values on the stack and use
 * krk_peek if the value is desired, as removing a value from the stack
 * may result in it being garbage collected.
 *
 * @return The value previously at the top of the stack.
 */
extern KrkValue krk_pop(void);

/**
 * @brief Peek down from the top of the stack.
 *
 * Obtains a value from the current thread's stack without modifying the stack.
 *
 * @param distance How far down from the top of the stack to peek (0 = the top)
 * @return The value from the stack.
 */
extern KrkValue krk_peek(int distance);

/**
 * @brief Swap the top of the stack of the value @p distance slots down.
 *
 * Exchanges the values at the top of the stack and @p distance slots from the top
 * without removing or shuffling anything in between.
 *
 * @param distance How from down from the top of the stack to swap (0 = the top)
 */
extern void krk_swap(int distance);

/**
 * @brief Get the name of the type of a value.
 * @memberof KrkValue
 *
 * Obtains the C string representing the name of the class
 * associated with the given value. Useful for crafting
 * exception messages, such as those describing TypeErrors.
 *
 * @param value Value to examine
 * @return Nul-terminated C string of the type of @p value
 */
extern const char * krk_typeName(KrkValue value);

/**
 * @brief Attach a native C function to an attribute table.
 * @memberof KrkTable
 *
 * Attaches the given native function pointer to an attribute table
 * while managing the stack shuffling and boxing of both the name and
 * the function object. If @p name begins with a '.', the native function
 * is marked as a method. If @p name begins with a ':', the native function
 * is marked as a dynamic property.
 *
 * @param table    Attribute table to attach to, such as @c &someInstance->fields
 * @param name     Nil-terminated C string with the name to assign
 * @param function Native function pointer to attach
 * @return A pointer to the object representing the attached function.
 */
extern KrkNative * krk_defineNative(KrkTable * table, const char * name, NativeFn function);

/**
 * @brief Attach a native dynamic property to an attribute table.
 * @memberof KrkTable
 *
 * Mostly the same as @c krk_defineNative, but ensures the creation of a dynamic property.
 * The intention of this function is to replace uses of defineNative with ":" names,
 * and replace specialized methods with @c KrkProperty* objects.
 *
 * @param table    Attribute table to attach to, such as @c &someInstance->fields
 * @param name     Nil-terminated C string with the name to assign
 * @param func     Native function pointer to attach
 * @return A pointer to the property object created.
 */
extern KrkNative * krk_defineNativeProperty(KrkTable * table, const char * name, NativeFn func);

/**
 * @brief Attach a value to an attribute table.
 * @memberof KrkTable
 *
 * Manages the stack shuffling and boxing of the name string when attaching
 * a value to an attribute table. Rather than using @c krk_tableSet, this is
 * the preferred method of supplying fields to objects from C code.
 *
 * @param table Attribute table to attach to, such as @c &someInstance->fields
 * @param name  Nil-terminated C string with the name to assign
 * @param obj   Value to attach.
 */
extern void krk_attachNamedValue(KrkTable * table, const char name[], KrkValue obj);

/**
 * @brief Attach an object to an attribute table.
 * @memberof KrkTable
 *
 * Manages the stack shuffling and boxing of the name string when attaching
 * an object to an attribute table. Rather than using @c krk_tableSet, this is
 * the preferred method of supplying fields to objects from C code.
 *
 * This is a convenience wrapper around @c krk_attachNamedValue.
 *
 * @param table Attribute table to attach to, such as @c &someInstance->fields
 * @param name  Nil-terminated C string with the name to assign
 * @param obj   Object to attach.
 */
extern void krk_attachNamedObject(KrkTable * table, const char name[], KrkObj * obj);

/**
 * @brief Raise an exception.
 *
 * Creates an instance of the given exception type, passing a formatted
 * string to the initializer. All of the core exception types take an option
 * string value to attach to the exception, but third-party exception types
 * may have different initializer signatures and need separate initialization.
 *
 * The created exception object is attached to the current thread state and
 * the @c KRK_THREAD_HAS_EXCEPTION flag is set.
 *
 * @param type Class pointer for the exception type, eg. @c vm.exceptions->valueError
 * @param fmt  Format string.
 * @return As a convenience to C extension authors, returns @c None
 */
extern KrkValue krk_runtimeError(KrkClass * type, const char * fmt, ...)
	__attribute__((format (printf, 2, 3)));

/**
 * @brief Get a pointer to the current thread state.
 *
 * Generally equivalent to @c &krk_currentThread, though @c krk_currentThread
 * itself may be implemented as a macro that calls this function depending
 * on the platform's thread support.
 *
 * @return Pointer to current thread's thread state.
 */
extern KrkThreadState * krk_getCurrentThread(void);

/**
 * @brief Continue VM execution until the next exit trigger.
 *
 * Resumes the VM dispatch loop, returning to the caller when
 * the next exit trigger event happens. Generally, callers will
 * want to set the current thread's exitOnFrame before calling
 * @c krk_runNext. Alternatively, see @c krk_callValue which manages
 * exit triggers automatically when calling function objects.
 *
 * @return Value returned by the exit trigger, generally the value
 *         returned by the inner function before the VM returned
 *         to the exit frame.
 */
extern KrkValue  krk_runNext(void);

/**
 * @brief Get the class representing a value.
 * @memberof KrkValue
 *
 * Returns the class object representing the type of a value.
 * This may be the direct class of an instance, or a pseudoclass
 * for other value types.
 *
 * @param value Reference value to examine.
 * @return A pointer to the value's type's class object.
 */
extern KrkClass * krk_getType(KrkValue value);

/**
 * @brief Determine if a class is an instance or subclass of a given type.
 * @memberof KrkValue
 *
 * Searches the class hierarchy of the given value to determine if it is
 * a subtype of @p type. As this chains through the inheritence tree, the
 * more deeply subclassed @p obj is, the longer it may take to determine
 * if it is a subtype of @p type. All types should eventually be subtypes
 * of the @ref object type, so this condition should not be checked. For
 * some types, convenience macros are available. If you need to check if
 * @p obj is a specific type, exclusive of subtypes, look at
 * @c krk_getType() instead of using this function.
 *
 * @param obj Value to examine.
 * @param type Class object to test for membership of.
 * @return 1 if @p obj is an instance of @p type or of a subclass of @p type
 */
extern int krk_isInstanceOf(KrkValue obj, const KrkClass * type);

/**
 * @brief Perform method binding on the stack.
 * @memberof KrkClass
 *
 * Performs attribute lookup from the class @p _class for @p name.
 * If @p name is not a valid method, the binding fails.
 * If @p name is a valid method, the method will be retrieved and
 * bound to the instance on the top of the stack, replacing it
 * with a @ref BoundMethod object.
 *
 * @param _class Class object to resolve methods from.
 * @param name   String object with the name of the method to resolve.
 * @return 1 if the method has been bound, 0 if binding failed.
 */
extern int krk_bindMethod(KrkClass * _class, KrkString * name);

/**
 * @brief Call a callable value in the current stack context.
 * @memberof KrkValue
 *
 * Executes the given callable object (function, bound method, object
 * with a @c \__call__() method, etc.) using @p argCount arguments from the stack.
 *
 * @param callee   Value referencing a callable object.
 * @param argCount Arguments to retreive from stack.
 * @param callableOnStack Whether @p callee is on the stack below the arguments,
 *                        which must be the case for bound methods, classes,
 *                        and instances, as that space will be used for the implicit
 *                        first argument passed to these kinds of callables.
 * @return An indicator of how the result should be obtained:
 *         1: The VM must be resumed to run managed code.
 *         2: The callable was a native function and result should be popped now.
 *         Else: The call failed. An exception may have already been set.
 */
extern int krk_callValue(KrkValue callee, int argCount, int callableOnStack);

/**
 * @brief Create a list object.
 * @memberof KrkList
 *
 * This is the native function bound to @c listOf
 */
extern KrkValue krk_list_of(int argc, const KrkValue argv[], int hasKw);

/**
 * @brief Create a dict object.
 * @memberof KrkDict
 *
 * This is the native function bound to @c dictOf
 */
extern KrkValue krk_dict_of(int argc, const KrkValue argv[], int hasKw);

/**
 * @brief Create a tuple object.
 * @memberof KrkTuple
 *
 * This is the native function bound to @c tupleOf
 */
extern KrkValue krk_tuple_of(int argc, const KrkValue argv[], int hasKw);

/**
 * @brief Create a set object.
 * @memberof Set
 *
 * This is the native function bound to @c setOf
 */
extern KrkValue krk_set_of(int argc, const KrkValue argv[], int hasKw);

/**
 * @brief Create a slice object.
 * @memberof KrkSlice
 */
extern KrkValue krk_slice_of(int argc, const KrkValue argv[], int hasKw);

/**
 * @brief Call a callable on the stack with @p argCount arguments.
 *
 * Calls the callable @p argCount stack entries down from the top
 * of the stack, passing @p argCount arguments. Resumes execution
 * of the VM for managed calls until they are completed. Pops
 * all arguments and the callable from the stack and returns the
 * return value of the call.
 *
 * @param argCount Arguments to collect from the stack.
 * @return The return value of the function.
 */
extern KrkValue krk_callStack(int argCount);

/**
 * @brief Call a closure or native function with @p argCount arguments.
 *
 * Calls the closure or native @p callable with arguments from the
 * top of the stack. @p argCount arguments are popped from the stack
 * and the return value of the call is returned.
 *
 * @param callable Closure or native function.
 * @param argCount Arguments to collect from the stack.
 * @return The return value of the function.
 */
extern KrkValue krk_callDirect(KrkObj * callable, int argCount);

/**
 * @brief Convenience function for creating new types.
 * @memberof KrkClass
 *
 * Creates a class object, output to @p _class, setting its name to @p name, inheriting
 * from @p base, and attaching it with its name to the fields table of the given @p module.
 *
 * @param module  Pointer to an instance for a module to attach to, or @c NULL to skip attaching.
 * @param _class  Output pointer to assign the new class object to.
 * @param name    Name of the new class.
 * @param base    Pointer to class object to inherit from.
 * @return A pointer to the class object, equivalent to the value assigned to @p _class.
 */
extern KrkClass * krk_makeClass(KrkInstance * module, KrkClass ** _class, const char * name, KrkClass * base);

/**
 * @brief Finalize a class by collecting pointers to core methods.
 * @memberof KrkClass
 *
 * Scans through the methods table of a class object to find special
 * methods and assign them to the class object's pointer table so they
 * can be referenced directly without performing hash lookups.
 *
 * @param _class Class object to finalize.
 */
extern void krk_finalizeClass(KrkClass * _class);

/**
 * @brief If there is an active exception, print a traceback to @c stderr
 *
 * This function is exposed as a convenience for repl developers. Normally,
 * the VM will call @c krk_dumpTraceback() itself if an exception is unhandled and no
 * exit trigger is current set. The traceback is obtained from the exception
 * object. If the exception object does not have a traceback, only the
 * exception itself will be printed. The traceback printer will attempt to
 * open source files to print faulting lines and may call into the VM if the
 * exception object has a managed implementation of @c \__str__.
 */
extern void krk_dumpTraceback(void);

/**
 * @brief Set up a new module object in the current thread.
 *
 * Creates a new instance of the module type and attaches a @c \__builtins__
 * reference to its fields. The module becomes the current thread's
 * main module, but is not directly attached to the module table.
 *
 * @param name Name of the module, which is assigned to @c \__name__
 * @return The instance object representing the module.
 */
extern KrkInstance * krk_startModule(const char * name);

/**
 * @brief Obtain a list of properties for an object.
 *
 * This is the native function bound to @c object.__dir__
 */
extern KrkValue krk_dirObject(int argc, const KrkValue argv[], int hasKw);

/**
 * @brief Load a module from a file with a specified name.
 *
 * This is generally called by the import mechanisms to load a single module and
 * will establish a module context internally to load the new module into, return
 * a KrkValue representing that module context through the @p moduleOut parameter.
 *
 * @param path      Dotted path of the module, used for file lookup.
 * @param moduleOut Receives a value with the module object.
 * @param runAs     Name to attach to @c \__name__ for this module, different from @p path
 * @param parent    Parent module object, if loaded from a package.
 * @return 1 if the module was loaded, 0 if an @ref ImportError occurred.
 */
extern int krk_loadModule(KrkString * path, KrkValue * moduleOut, KrkString * runAs, KrkValue parent);

/**
 * @brief Load a module by a dotted name.
 *
 * Given a package identifier, attempt to the load module into the module table.
 * This is a thin wrapper around `krk_importModule()`.
 *
 * @param name String object of the dot-separated package path to import.
 * @return 1 if the module was loaded, 0 if an @ref ImportError occurred.
 */
extern int krk_doRecursiveModuleLoad(KrkString * name);

/**
 * @brief Load the dotted name @p name with the final element as @p runAs
 *
 * If @p name was imported previously with a name different from @p runAs,
 * it will be imported again with the new name; this may result in
 * unexpected behaviour. Generally, @p runAs is used to specify that the
 * module should be run as `__main__`.
 *
 * @param name Dotted path name of a module.
 * @param runAs Alternative name to attach to @c \__name__ for the module.
 * @return 1 on success, 0 on failure.
 */
extern int krk_importModule(KrkString * name, KrkString * runAs);

/**
 * @brief Determine the truth of a value.
 * @memberof KrkValue
 *
 * Determines if a value represents a "falsey" value.
 * Empty collections, 0-length strings, False, numeric 0,
 * None, etc. are "falsey". Other values are generally "truthy".
 *
 * @param value Value to examine.
 * @return 1 if falsey, 0 if truthy
 */
extern int krk_isFalsey(KrkValue value);

/**
 * @brief Obtain a property of an object by name.
 * @memberof KrkValue
 *
 * This is a convenience function that works in essentially the
 * same way as the OP_GET_PROPERTY instruction.
 *
 * @param value Value to examine.
 * @param name  C-string of the property name to query.
 * @return The requested property, or None with an @ref AttributeError
 *         exception set in the current thread if the attribute was
 *         not found.
 */
extern KrkValue krk_valueGetAttribute(KrkValue value, char * name);

/**
 * @brief See @ref krk_valueGetAttribute
 */
extern KrkValue krk_valueGetAttribute_default(KrkValue value, char * name, KrkValue defaultVal);

/**
 * @brief Set a property of an object by name.
 * @memberof KrkValue
 *
 * This is a convenience function that works in essentially the
 * same way as the OP_SET_PROPERTY instruction.
 *
 * @param owner The owner of the property to modify.
 * @param name  C-string of the property name to modify.
 * @param to    The value to assign.
 * @return The set value, or None with an @ref AttributeError
 *         exception set in the current thread if the object can
 *         not have a property set.
 */
extern KrkValue krk_valueSetAttribute(KrkValue owner, char * name, KrkValue to);

/**
 * @brief Delete a property of an object by name.
 * @memberof KrkValue
 *
 * This is a convenience function that works in essentially the
 * same way as the OP_DEL_PROPERTY instruction.
 *
 * @param owner The owner of the property to delete.
 * @param name  C-string of the property name to delete.
 */
extern KrkValue krk_valueDelAttribute(KrkValue owner, char * name);

/**
 * @brief Concatenate two strings.
 *
 * This is a convenience function which calls @c str.__add__ on the top stack
 * values. Generally, this should be avoided - use @c StringBuilder instead.
 */
extern void krk_addObjects(void);

/**
 * @brief Compare two values, returning @ref True if the left is less than the right.
 *
 * This is equivalent to the opcode instruction OP_LESS.
 */
extern KrkValue krk_operator_lt(KrkValue,KrkValue);

/**
 * @brief Compare to values, returning @ref True if the left is greater than the right.
 *
 * This is equivalent to the opcode instruction OP_GREATER.
 */
extern KrkValue krk_operator_gt(KrkValue,KrkValue);

/**
 * @brief Set the maximum recursion call depth.
 */
extern void krk_setMaximumRecursionDepth(size_t maxDepth);

/**
 * @brief Call a native function using a reference to stack arguments safely.
 *
 * Passing the address of the stack to a native function directly would be unsafe:
 * the stack can be reallocated at any time through pushes. To allow for native functions
 * to be called with arguments from the stack safely, this wrapper holds on to a reference
 * to the stack at the call time, unless one was already held by an outer call; if a
 * held stack is reallocated, it will be freed when execution returns to the call
 * to @c krk_callNativeOnStack that holds it.
 */
KrkValue krk_callNativeOnStack(size_t argCount, const KrkValue *stackArgs, int hasKw, NativeFn native);
