#include <cctype>
#include <cstddef>
#include <string>
#include <iostream>

static std::size_t HashLower(const std::string& value)
{
    constexpr std::size_t kFnvOffsetBasis = 14695981039346656037ULL;
    constexpr std::size_t kFnvPrime = 1099511628211ULL;

    std::size_t result = kFnvOffsetBasis;
    for (unsigned char ch : value) {
        result ^= static_cast<std::size_t>(std::tolower(ch));
        result *= kFnvPrime;
    }
    return result;
}

int main(){
    for (auto s: {"FACE_RLYThin","FACE_RLYMuscular","FACE_RLYHeavy"}){
        std::cout<<s<<" "<<HashLower(s)<<"\n";
    }
}
