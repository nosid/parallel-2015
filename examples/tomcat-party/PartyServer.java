import java.io.IOException;
import java.util.concurrent.BrokenBarrierException;
import java.util.concurrent.CyclicBarrier;

import javax.servlet.http.HttpServlet;
import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;

import org.apache.catalina.Context;
import org.apache.catalina.startup.Tomcat;
import org.apache.coyote.http11.Http11NioProtocol;

public final class PartyServer {

    public static void main(String... args) throws Exception {
        if (args.length != 1) {
            throw new RuntimeException("expected exactly one argument");
        }
        int concurrency = Integer.parseInt(args[0]);
        CyclicBarrier barrier = new CyclicBarrier(concurrency);
        startTomcat(concurrency, () -> {
            try {
                barrier.await();
            } catch (InterruptedException|BrokenBarrierException e) {
                Thread.currentThread().interrupt();
            }
        });
        for (;;) {
            Thread.sleep(1000);
        }
    }

    private static void startTomcat(int concurrency, Runnable runnable) throws Exception {
        Tomcat tomcat = new Tomcat();
        Context ctx = tomcat.addContext("", null);
        Tomcat.addServlet(ctx, "root", new HttpServlet() {
            private static final long serialVersionUID = 1L;
            @Override
            public void doGet(HttpServletRequest request, HttpServletResponse response) {
                runnable.run();
            }
        });
        ctx.addServletMapping("/", "root");
        Http11NioProtocol protocol = (Http11NioProtocol) tomcat.getConnector().getProtocolHandler();
        protocol.setPollerThreadCount(4); // default: 2
        protocol.setProcessorCache(-1); // default: 200
        protocol.setMaxThreads(concurrency); // default: 200
        protocol.setMaxConnections(concurrency); // default: 10000
        protocol.setMinSpareThreads(concurrency); // default: 10
        protocol.setBacklog(1 << 14); // default: 100
        // protocol.setAcceptorThreadCount(4); // default: 1 (since tomcat9)
        tomcat.start();
    }

}
