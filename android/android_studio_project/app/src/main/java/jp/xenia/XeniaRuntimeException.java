package jp.xenia;

/**
 * Base class for all unchecked exceptions thrown by the Xenia project components.
 */
public class XeniaRuntimeException extends RuntimeException {
    public XeniaRuntimeException() {
    }

    public XeniaRuntimeException(String name) {
        super(name);
    }

    public XeniaRuntimeException(String name, Throwable cause) {
        super(name, cause);
    }

    public XeniaRuntimeException(Exception cause) {
        super(cause);
    }
}
