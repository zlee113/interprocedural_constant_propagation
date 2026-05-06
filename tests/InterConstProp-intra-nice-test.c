int foo()
{
    int x = 3;
    int y = 7;
    return x + y;
}

int bar()
{
    int a = 4;
    int b = 6;
    int c = a * b;
    int d = c + 1;
    return d;
}

int main()
{
    int a = foo();
    int b = bar();
    return a + b;
}