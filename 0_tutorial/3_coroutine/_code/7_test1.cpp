#include <boost/coroutine2/all.hpp>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

struct FinalEOL {
    ~FinalEOL() { std::cout << std::endl; }
};

const int num = 5, width = 15;

int main() {
    boost::coroutines2::coroutine<std::string>::push_type writer(
        [&](boost::coroutines2::coroutine<std::string>::pull_type& in) {
            // finish the last line when we leave by whatever means
            FinalEOL eol;
            // pull values from upstream, lay them out 'num' to a line
            for (;;) {
                for (int i = 0; i < num; ++i) {
                    // when we exhaust the input, stop
                    if (!in) return;
                    std::cout << std::setw(width) << in.get();
                    // now that we've handled this item, advance to next
                    in();
                }
                // after 'num' items, line break
                std::cout << std::endl;
            }
        });

    std::vector<std::string> words{
        "peas",     "porridge", "hot", "peas", "porridge", "cold", "peas",
        "porridge", "in",       "the", "pot",  "nine",     "days", "old"};

    for (const auto& word : words) {
        writer(word);
    }

    return 0;
}