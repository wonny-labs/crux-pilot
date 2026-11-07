int crux_configuration_conformance(void);
int crux_configuration_string_conformance(void);
int main(void)
{
    int scalar = crux_configuration_conformance();
    int string = crux_configuration_string_conformance();
    return scalar != 0 || string != 0;
}
