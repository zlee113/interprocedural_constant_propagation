int multiply(int a, int b)
{
    return a * b;
}

int scale(int x)
{
    return multiply(x, 3);
}

int main()
{
    return scale(4); // 12
}