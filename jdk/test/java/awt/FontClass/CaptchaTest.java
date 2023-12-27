/*
 * Copyright © 2023 Xiaotao NAN. All rights reserved.
 */
import java.awt.Graphics2D;
import java.awt.Color;
import java.awt.Font;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.util.Random;

/*
 * @test
 * @summary Check if the captcha can be successfully generated
 * @author XiaotaoNAN
 * I8ME2N(https://gitee.com/openeuler/bishengjdk-8/issues/I8ME2N?from=project-issue)
 */

public class CaptchaTest {

    /**
     * Check if the captcha can be successfully generated.
     * @param n the number of digits int the captcha.
     * @param fontName the font name.
     * @throws IOException
     */
    public static String captchaTest(int n,String fontName) throws IOException {
        int width = 100, height = 50;
        BufferedImage image = new BufferedImage(width, height, BufferedImage.TYPE_INT_RGB);
        Graphics2D g = image.createGraphics();
        g.setColor(Color.LIGHT_GRAY);
        g.fillRect(0, 0, width, height);
        String chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        Random random = new Random();
        StringBuilder sbuffer = new StringBuilder();
        for (int i = 0; i < n; i++) {
            int index = random.nextInt(chars.length());
            char c = chars.charAt(index);
            sbuffer.append(c);
            g.setColor(new Color(random.nextInt(255), random.nextInt(255), random.nextInt(255)));
            g.setFont(new Font(fontName, Font.BOLD, 25));
            g.drawString(Character.toString(c), 20 + i * 15, 25);
        }
        image.flush();
        g.dispose();
        return sbuffer.toString();
    }

    public static void main(String[] args) throws IOException {
        String captcha =  captchaTest(4,"Times New Roman");
        System.out.println(captcha);
    }

}
