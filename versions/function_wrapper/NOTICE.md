# Function-wrapper provenance

The archived `function_wrapper.h` and `LightweightFunctionReference.h` editions
contain a `function_ref` implementation derived from the Stack Overflow answer
"std::function with static allocation in c++" by HeroicKatora, posted
2017-07-13:

https://stackoverflow.com/a/45091884

That answer is licensed CC BY-SA 3.0 because it was contributed between
2011-04-08 and 2018-05-02:

https://creativecommons.org/licenses/by-sa/3.0/
https://stackoverflow.com/help/licensing

The archived files are historical source editions and are not covered by the
repository-root Unlicense. `include/function_ref.h` is a fresh implementation
using a different storage/invocation design; it does not copy the archived
implementation.
