import javax.crypto.Cipher;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;
import java.lang.reflect.Executable;
import java.util.Arrays;
import java.util.Random;
import java.util.concurrent.Callable;

/**
 * @test
 * @bug 8308682
 * @summary Check for 128-bit AES/CTR wraparound
 * @library /testlibrary /testlibrary/whitebox /compiler/whitebox /compiler/testlibrary
 * @build CTR_Wraparound
 * @run main ClassFileInstaller sun.hotspot.WhiteBox
 *                              sun.hotspot.WhiteBox$WhiteBoxPermission
 * @run main/othervm -Xbootclasspath/a:. 
 *                   -XX:+UnlockDiagnosticVMOptions
 *                   -XX:+WhiteBoxAPI 
 *                   CTR_Wraparound 32
 * @run main/othervm -Xbootclasspath/a:.
 *                   -XX:+UnlockDiagnosticVMOptions
 *                   -XX:+WhiteBoxAPI
 *                   CTR_Wraparound 1009
 * @run main/othervm -Xbootclasspath/a:.
 *                   -XX:+UnlockDiagnosticVMOptions
 *                   -XX:+WhiteBoxAPI
 *                   CTR_Wraparound 2048
 */

public class CTR_Wraparound extends CompilerWhiteBoxTest {
    private static final String ALGO = "AES/CTR/NoPadding";
    private static final int LOOPS = 100000;
    private int length;
    private int maxOffset;

    public CTR_Wraparound(int len,int offset){
        super(new CTR_WraparoundTestCase());
        length = len;
        maxOffset = offset;
    }

    public static class CTR_WraparoundTestCase implements TestCase {

        public String name() {
            return "CTR_WraparoundTestCase";
        }

        public Executable getExecutable(){
            try {
                return Class.forName("com.sun.crypto.provider.CounterMode").getDeclaredMethod("implCrypt", byte[].class, int.class, int.class, byte[].class, int.class);
            } catch (NoSuchMethodException e) {
                throw new RuntimeException("Test bug, method unavailable. " + e);
            } catch (ClassNotFoundException e) {
                throw new RuntimeException("Test bug, class unavailable. " + e);
            }
        }

        public Callable<Integer> getCallable() {
            return null;
        }

        public boolean isOsr() {
            return false;
        }

    }

    private static boolean isServerVM(String VMName) { return VMName.toLowerCase().contains("server");}
    


    protected static boolean checkIntrinsicForCompilationLevel(Executable method, int compLevel) {
        boolean intrinsicEnabled = Boolean.valueOf(getVMOption("UseAESCTRIntrinsics"));
        boolean intrinsicAvailable = WHITE_BOX.isIntrinsicAvailable(method,
                compLevel);
        if(intrinsicAvailable && intrinsicEnabled){
            return true;
        }
        return false;
    }
    
    public static void main(String[] args) throws Exception {
        int length = Integer.parseInt(args[0]);
        int maxOffset = 60;
        if (args.length > 1) {
            maxOffset = Integer.parseInt(args[1]);
            System.out.println("InitialOffset = " + maxOffset);
        }
        new CTR_Wraparound(length,maxOffset).test();
    }

    @Override
    protected void test() throws Exception {

        String VMName = System.getProperty("java.vm.name");
        Executable intrinsicMethod = testCase.getExecutable();
        boolean isIntrinsicEnabled = false;
        if (isServerVM(VMName)) {
            if (TIERED_COMPILATION) {
                isIntrinsicEnabled = checkIntrinsicForCompilationLevel(intrinsicMethod, COMP_LEVEL_SIMPLE);
            }
            isIntrinsicEnabled = checkIntrinsicForCompilationLevel(intrinsicMethod, COMP_LEVEL_FULL_OPTIMIZATION);
        } else {
            isIntrinsicEnabled = checkIntrinsicForCompilationLevel(intrinsicMethod, COMP_LEVEL_SIMPLE);
        }
        if(!isIntrinsicEnabled){
            return;
        }


        long SEED = Long.getLong("jdk.test.lib.random.seed", new Random().nextLong());
        Random random = new Random(SEED);

        byte[] keyBytes = new byte[32];
        Arrays.fill(keyBytes, (byte)0xff);
        SecretKeySpec key = new SecretKeySpec(keyBytes, "AES");

        byte[] ivBytes = new byte[16];

        Arrays.fill(ivBytes, (byte)0xff);

        byte[][] plaintext = new byte[maxOffset][];
        byte[][] ciphertext = new byte[maxOffset][];

        for (int offset = 0; offset < maxOffset; offset++) {
            ivBytes[ivBytes.length - 1] = (byte)-offset;
            IvParameterSpec iv = new IvParameterSpec(ivBytes);

            Cipher encryptCipher = Cipher.getInstance(ALGO);
            Cipher decryptCipher = Cipher.getInstance(ALGO);

            encryptCipher.init(Cipher.ENCRYPT_MODE, key, iv);
            decryptCipher.init(Cipher.DECRYPT_MODE, key, iv);

            plaintext[offset] = new byte[length];
            ciphertext[offset] = new byte[length];
            random.nextBytes(plaintext[offset]);

            byte[] decrypted = new byte[length];

            encryptCipher.doFinal(plaintext[offset], 0, length, ciphertext[offset]);
            decryptCipher.doFinal(ciphertext[offset], 0, length, decrypted);

            if (!Arrays.equals(plaintext[offset], decrypted)) {
                throw new Exception("mismatch in setup at offset " + offset);
            }
        }

        for (int offset = 0; offset < maxOffset; offset++) {
            ivBytes[ivBytes.length - 1] = (byte)-offset;
            IvParameterSpec iv = new IvParameterSpec(ivBytes);

            Cipher encryptCipher = Cipher.getInstance(ALGO);

            encryptCipher.init(Cipher.ENCRYPT_MODE, key, iv);

            byte[] encrypted = new byte[length];

            for (int i = 0; i < LOOPS; i++) {
                encryptCipher.doFinal(plaintext[offset], 0, length, encrypted);
                if (!Arrays.equals(ciphertext[offset], encrypted)) {
                    throw new Exception("array mismatch at offset " + offset
                            + " with length " + length);
                }
            }
        }
    }
}
