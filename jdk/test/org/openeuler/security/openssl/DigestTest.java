import org.openeuler.security.openssl.KAEProvider;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.Security;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

/**
 * @test
 * @summary Basic test for MD5 SHA256 SHA384
 * @requires os.arch=="aarch64"
 * @run main/othervm DigestTest
 */
public class DigestTest {
    private static String PLAIN_TEXT = "hello world";

    private static Map<String, byte[]> alg = new HashMap<String, byte[]>();

    static {
        alg.put("MD5", new byte[] {94, -74, 59, -69, -32, 30, -18, -48, -109, -53, 34, -69, -113, 90, -51, -61});
        alg.put(
                "SHA-256",
                new byte[] {
                        -71, 77, 39, -71, -109, 77, 62, 8, -91, 46, 82, -41, -38, 125, -85, -6,
                        -60, -124, -17, -29, 122, 83, -128, -18, -112, -120, -9, -84, -30, -17, -51, -23
                });
        alg.put(
                "SHA-384",
                new byte[] {
                        -3, -67, -114, 117, -90, 127, 41, -9, 1, -92, -32, 64, 56, 94, 46, 35,
                        -104, 99, 3, -22, 16, 35, -110, 17, -81, -112, 127, -53, -72, 53, 120, -77,
                        -28, 23, -53, 113, -50, 100, 110, -3, 8, 25, -35, -116, 8, -115, -31, -67
                });
        alg.put(
                "SM3",
                new byte[] {
                        68, -16, 6, 30, 105, -6, 111, -33, -62, -112, -60, -108, 101, 74, 5,
                        -36, 12, 5, 61, -89, -27, -59, 43, -124, -17, -109, -87, -42, 125, 63,
                        -1, -120
                });
    }

    public static void main(String[] args) throws Exception {
        Security.insertProviderAt(new KAEProvider(), 1);
        for (String key : alg.keySet()) {
            test(PLAIN_TEXT, key, alg.get(key));
        }
    }

    public static void test(String plainText, String algo, byte[] expectRes) throws Exception {
        MessageDigest md = MessageDigest.getInstance(algo);
        md.update(plainText.getBytes(StandardCharsets.UTF_8));
        byte[] res = md.digest();
        if (!Arrays.equals(res, expectRes)) {
            throw new RuntimeException(algo + " failed");
        }
    }
}
