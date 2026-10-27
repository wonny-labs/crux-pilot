/* Test composition; this file and the private fault store are not portable consumers. */
void config_fault_reset(void);
int configuration_consumer(void);
int main(void)
{
    config_fault_reset();
    return configuration_consumer();
}
