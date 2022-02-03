package jp.xenia;

/**
 * Base class for all unchecked exceptions thrown by the Xenia project components.
 */
public class XeniaRuntimeException extends RuntimeException {
    public XeniaRuntimeException() {
    }

    public XeniaRuntimeException(final String name) {
        super(name);
    }

    public XeniaRuntimeException(final String name, final Throwable cause) {
        super(name, cause);
    }

    public XeniaRuntimeException(final Exception cause) {
        super(cause);
    }
}
