import org.openeuler.security.openssl.KAEProvider;

import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;
import java.nio.charset.StandardCharsets;
import java.security.Security;
import java.util.Arrays;

/**
 * @test
 * @summary Basic test for AES/GCM Iv
 * @requires os.arch=="aarch64"
 * @run main KAEGcmIvLenTest
 */
public class KAEGcmIvLenTest {
    private static String plainText = "helloworldhellow";  // 16bytes for NoPadding
    private static String shortPlainText = "helloworld"; // 5 bytes for padding
    private static SecretKeySpec ks = new SecretKeySpec("AESEncryptionKey".getBytes(StandardCharsets.UTF_8), "AES");  // key has 16 bytes
    private static int[] ivLens = {12, 16};
    public static void main(String[] args) throws Exception {
        Security.addProvider(new KAEProvider());
        for (int ivLen : ivLens) {
            testGcm(plainText,"AES/GCM/NoPadding", "KAEProvider", "SunJCE", ivLen);
            testGcm(plainText,"AES/GCM/NoPadding", "SunJCE", "KAEProvider", ivLen);
            testGcm(shortPlainText,"AES/GCM/PKCS5Padding", "KAEProvider", "SunJCE", ivLen);
            testGcm(shortPlainText,"AES/GCM/PKCS5Padding", "SunJCE", "KAEProvider", ivLen);
        }

    }

    private static void testGcm(String plainText, String algo, String encryptProvider, String decryptProvider, int ivLen) throws Exception {
        Cipher enCipher = Cipher.getInstance(algo, encryptProvider);
        enCipher.init(Cipher.ENCRYPT_MODE, ks, getIv(ivLen));
        byte[] cipherText = enCipher.doFinal(plainText.getBytes());

        Cipher deCipher = Cipher.getInstance(algo, decryptProvider);
        deCipher.init(Cipher.DECRYPT_MODE, ks, getIv(ivLen));
        byte[] origin = deCipher.doFinal(cipherText);

        if (!Arrays.equals(plainText.getBytes(), origin)) {
            throw new RuntimeException("gcm decryption failed, algo = " + algo);
        }
    }

    private static GCMParameterSpec getIv(int ivLen) {
        if (ivLen == 16) {
            return new GCMParameterSpec(128, "abcdefghabcdefgh".getBytes(StandardCharsets.UTF_8));
        }
        return new GCMParameterSpec(96, "abcdefghabcd".getBytes(StandardCharsets.UTF_8));
    }
}
