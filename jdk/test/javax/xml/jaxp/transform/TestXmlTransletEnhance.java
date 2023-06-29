/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2012-2023. All rights reserved.
 */


/* @test
 * @summary a test for xml translet enhance
 * @library /lib/testlibrary
 * @run main TestXmlTransletEnhance
 */

import javax.xml.transform.TransformerFactory;
import com.sun.org.apache.xalan.internal.xsltc.trax.TransformerFactoryImpl;
import java.lang.reflect.Field;
import static jdk.testlibrary.Asserts.assertEquals;

public class TestXmlTransletEnhance {
    static final boolean expectedResult = true;

    public static void main(String[] args) throws InterruptedException {

        Thread thread = new Mythread("BenchmarkThread xml ");
        thread.start();
        thread.join();
        boolean ret = SharedData.getInstance().getResult();
        assertEquals(ret, expectedResult);

    }

    static class Mythread extends Thread {
        Mythread(String name){
            super(name);
        }

        @Override
        public void run(){

            try {

                TransformerFactory tf = TransformerFactory.newInstance();
                TransformerFactoryImpl transformer = new TransformerFactoryImpl();
                Class<?> clazz = transformer.getClass();

                Field generateTransletFiled = clazz.getDeclaredField("_generateTranslet");
                Field autoTransletFiled = clazz.getDeclaredField("_autoTranslet");

                generateTransletFiled.setAccessible(true);
                autoTransletFiled.setAccessible(true);

                boolean value1 = (boolean)generateTransletFiled.get(transformer);
                boolean value2 = (boolean)autoTransletFiled.get(transformer);

                SharedData.getInstance().setResult(value1 && value2);

            } catch (NoSuchFieldException| IllegalAccessException  | SecurityException | IllegalArgumentException e) {
                e.printStackTrace();
            }
        }
    }

    static class SharedData {
        private static SharedData instance;
        private boolean result;

        private SharedData() {
        }

        public static synchronized SharedData getInstance() {
            if (instance == null) {
                instance = new SharedData();
            }
            return instance;
        }

        public synchronized boolean getResult() {
            return result;
        }

        public synchronized void setResult(boolean result) {
            this.result = result;
        }
    }
}
