#include <stdio.h>

int get_ten()
{
    return 10;
}

int double_ten()
{
    int x = get_ten();
    return x + x;
}

int add(int a, int b)
{
    return a + b;
}

int combine()
{
    int x = get_ten();
    int y = 5;
    return add(x, y);
}

int main()
{
    int a = get_ten();
    int b = double_ten();
    int c = combine();
    printf("%d %d %d\n", a, b, c);
    return 0;
}