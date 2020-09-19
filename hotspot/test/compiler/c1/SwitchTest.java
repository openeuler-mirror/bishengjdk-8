/*
 * @test
 * @bug 8203196
 * @summary C1 emits incorrect code due to integer overflow in _tableswitch keys
 * @run main/othervm -Xcomp SwitchTest
 */
public class SwitchTest {
    public static void main(String args[]) throws Exception {
        int test2 = 2147483647;
        int check2 = 0;
        switch (test2) {
            case 2147483640:
                check2 = 2147483640;
                break;
            case 2147483641:
                check2 = 2147483641;
                break;
            case 2147483642:
                check2 = 2147483642;
                break;
            case 2147483643:
                check2 = 2147483643;
                break;
            case 2147483644:
                check2 = 2147483644;
                break;
            case 2147483645:
                check2 = 2147483645;
                break;
            case 2147483646:
                check2 = 2147483646;
                break;
            case 2147483647:
                check2 = 2147483647;
                break;
            default:
                check2 = 123456;
                break;
        }
        if (check2 != test2) {
            System.out.println("choose a wrong case");
            throw new Exception();
        }

    }
}