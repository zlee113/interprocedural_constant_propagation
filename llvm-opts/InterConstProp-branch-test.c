int get_flag()
{
    return 1;
}

int main()
{
    int f = get_flag();

    /* Always takes if statement */
    if (f == 1)
    {
        return 100;
    }
    /* Never hit*/
    else
    {
        return 200;
    }
}