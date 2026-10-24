int external_consumer(void);
int external_cpp_consumer(void);
int main(void) {
    return external_cpp_consumer() || external_consumer();
}
