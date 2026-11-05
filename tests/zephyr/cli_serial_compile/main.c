/* Compile evidence only. The image does not call the command line, because Access has no Zephyr
 * composition yet, and it must never be flashed as evidence of a console. It exists so that the
 * shell rule, the Serial binding and the production image check apply to a real build. */
int main(void)
{
    return 0;
}
