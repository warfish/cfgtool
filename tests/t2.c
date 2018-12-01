int func(int a)
{
    int res = 1;
    while (a > 0) {
        res = res * a;
        a = a - 1;
    }

    return res;
}

