![](/assets/images/merton-bot.png)

# Merton Market Maker: C++26 Reflection + PyBind11
**Solving the Speed-to-Market vs Speed-to-Book Tension in Algorithmic Trading.**

---

⚠️ **The "No Alpha" Disclaimer**

This project is an engineering demonstration. The included Merton Jump Diffusion (MJD) algorithm is a standard, textbook model used to provide a real-world context.
* This is not a money-printing bot.
* It exposes no novel alpha.
* The value is in the infrastructure, not the strategy.

---
## A Practical Hybrid Workflow
The algorithmic trading world is often split into two camps:

1. **Speed-to-Market (Python)**: Rapid iteration, research, and backtesting.
2. **Speed-to-Book (C++)**: Low-latency execution and high-performance pricing math.

While it has always been possible to bridge these worlds using PyBind11 (or CFFI etc), the manual "boilerplate tax" is usually too cumbersome for day-to-day use. Quants writing pricing models shouldn't have to manually update a binding layer just to see the result in their Python strategy.  A quant-dev with both C++ and Python shouldn't have to be brought in.

C++26 Reflection (P2996) changes this. It is *compiler supported* compile-time programming. We can automate the "bridge" entirely and reliably. This project demonstrates how reflection makes the hybrid Python/C++ model practical for daily development and provides an end-to-end working example.

## Through the Mirror into a New C++ World
Instead of writing a `.def()` for every C++ function, I used a generic reflection loop. When you add or change a public method in your C++ math engine, the Python bindings update automatically at compile-time.
```c++
template <typename T>
void bind_reflected_member_functions(py::class_<T>& cl) {
    // 1. Inspect the class at compile-time
    constexpr auto members = std::define_static_array(
        members_of(^^T, access_context::current()));

    // 2. Filter and bind automatically
    template for (constexpr auto m : members) {
        if constexpr (is_public(m) && is_function(m) && !is_constructor(m)) {
            cl.def(identifier_of(m).data(), &[:m:]);
        }
    }
```
*Now, any public method added to the C++ engine is instantly available in the Python execution environment without a single extra line of binding code.*

Note the new C++ syntax: the "double-hat" reflection operator `^^T` which converts a type into a `meta` (reflected) "value", and the "splicer" `[:m:]` converts constituents back to run-time expressions.

---
## Why Merton Jump Diffusion?
I chose the MJD model because its math is heavy - it requires infinite series summations that would crawl in pure Python. It perfectly illustrates the "Speed-to-Book" necessity for the math, while the high-level trading logic benefits from Python's "Speed-to-Market."

## Technical Stack
Since C++26 reflection requires the experimental Bloomberg Clang P2996 fork, I  containerized the toolchain and provided a `justfile` to make it accessible without a manual compiler build.

### Prerequisites
* [Docker](https://www.docker.com/)
* [`just`](https://github.com/casey/just)

See [BUILD](/cpp/BUILD.md) for complete information on building and running.

## References & Further Reading
* Blog Post: [Stop Choosing: Get C++ Performance in Python Algos with C++26](https://profitview.net/blog/cpp26-reflection-python-algo-trading)
* [P2996 - Reflection for C++26](https://wg21.link/p2996)
* [Callum Piper](https://www.linkedin.com/in/callum-piper-3691373/)'s [talk](https://youtu.be/SJ0NFLpR9vE) from ACCU 2025.
* Reference deployment: [ProfitView](https://profitview.net/).
* See [THEORY](/cpp/THEORY.md)

If you need more information or are interested in building a system using C++ reflection, feel free to contact me: 
* [LinkedIn](https://www.linkedin.com/in/rthickling/)
* richard@profitview.net
* richard.t.hickling@gmail.com