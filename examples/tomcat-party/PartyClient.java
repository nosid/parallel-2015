import java.io.IOException;
import java.net.InetAddress;
import java.net.Socket;
import java.util.concurrent.atomic.AtomicInteger;

public final class PartyClient {

    public static void main(String... args) throws InterruptedException {
        if (args.length != 1) {
            throw new RuntimeException("expected exactly one argument");
        }
        int concurrency = Integer.parseInt(args[0]);
        AtomicInteger total = new AtomicInteger();
        for (int i = 0; i < concurrency; ++i) {
            new Thread(() -> run(total)).start();
            Thread.sleep(1);
        }
        for (long start = System.currentTimeMillis();;) {
            Thread.sleep(1000);
            double elapsed = (System.currentTimeMillis() - start) / 1000.0;
            System.out.printf("%.0f\n", total.get() / elapsed);
        }
    }

    private static void run(AtomicInteger total) {
        try {
            InetAddress address = InetAddress.getLoopbackAddress();
            int port = 8080;
            byte[] request = "GET / HTTP/1.0\r\n\r\n".getBytes();
            byte[] buffer = new byte[1 << 14];
            for (;;) {
                try (Socket s = new Socket(address, port)) {
                    s.setTcpNoDelay(true);
                    s.getOutputStream().write(request);
                    while (s.getInputStream().read(buffer) > 0) { }
                }
                total.incrementAndGet();
            }
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }

}
