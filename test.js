function* foo(a) {
    x = yield a;
    y = x + 1;
    yield y;
}

func = foo(123);
console.log(func);
console.log(func.next(0));
