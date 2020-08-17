#include <AK/String.h>
#include <AK/StringBuilder.h>
#include <stdio.h>

int main()
{
    StringBuilder builder;

    for (auto i = 0; i < 100000; ++i) {
        builder.append("fooo");
        builder.append('x');
    }

    auto huge_string = builder.to_string();

    return 0;
}
