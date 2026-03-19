int main() {
    void f(void (*cb)(int)) {}
    f( ({ void __fn(int x) {} __fn; }) );
    return 0;
}
