/*
 * Copyright (c) 1996, 2013, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package java.util.zip;

import java.io.SequenceInputStream;
import java.io.ByteArrayInputStream;
import java.io.FilterInputStream;
import java.io.InputStream;
import java.io.IOException;
import java.io.EOFException;

/**
 * This class implements a stream filter for reading compressed data in
 * the GZIP file format.
 *
 * @see         InflaterInputStream
 * @author      David Connelly
 *
 */
public
class GZIPInputStream extends InflaterInputStream {
    /**
     * CRC-32 for uncompressed data.
     */
    protected CRC32 crc = new CRC32();

    /**
     * Indicates end of input stream.
     */
    protected boolean eos;

    private boolean closed = false;

    /**
     * The field is mainly used to support the KAE-zip feature.
     */
    private static boolean GZIP_USE_KAE = false;

    private static int WINDOWBITS = 31;

    private static int FLUSHKAE = 2;

    static {
        if ("aarch64".equals(System.getProperty("os.arch"))) {
            GZIP_USE_KAE = Boolean.parseBoolean(System.getProperty("GZIP_USE_KAE", "false"));
            WINDOWBITS = Integer.parseInt(System.getProperty("WINDOWBITS", "31"));
            FLUSHKAE = Integer.parseInt(System.getProperty("FLUSHKAE", "2"));
        }
    }

    /**
     * Check to make sure that this stream has not been closed
     */
    private void ensureOpen() throws IOException {
        if (closed) {
            throw new IOException("Stream closed");
        }
    }

    /**
     * Creates a new input stream with the specified buffer size.
     * @param in the input stream
     * @param size the input buffer size
     *
     * @exception ZipException if a GZIP format error has occurred or the
     *                         compression method used is unsupported
     * @exception IOException if an I/O error has occurred
     * @exception IllegalArgumentException if {@code size <= 0}
     */
    public GZIPInputStream(InputStream in, int size) throws IOException {
        super(in, GZIP_USE_KAE ? new Inflater(WINDOWBITS, FLUSHKAE) : new Inflater(true), size);
        usesDefaultInflater = true;

        // When GZIP_USE_KAE is true, the header of the file is readed
        // through the native zlib library, not in java code.
        if (GZIP_USE_KAE) return;

        readHeader(in);
    }

    /**
     * Creates a new input stream with a default buffer size.
     * @param in the input stream
     *
     * @exception ZipException if a GZIP format error has occurred or the
     *                         compression method used is unsupported
     * @exception IOException if an I/O error has occurred
     */
    public GZIPInputStream(InputStream in) throws IOException {
        this(in, 512);
    }

    /**
     * Reads uncompressed data into an array of bytes. If <code>len</code> is not
     * zero, the method will block until some input can be decompressed; otherwise,
     * no bytes are read and <code>0</code> is returned.
     * @param buf the buffer into which the data is read
     * @param off the start offset in the destination array <code>b</code>
     * @param len the maximum number of bytes read
     * @return  the actual number of bytes read, or -1 if the end of the
     *          compressed input stream is reached
     *
     * @exception  NullPointerException If <code>buf</code> is <code>null</code>.
     * @exception  IndexOutOfBoundsException If <code>off</code> is negative,
     * <code>len</code> is negative, or <code>len</code> is greater than
     * <code>buf.length - off</code>
     * @exception ZipException if the compressed input data is corrupt.
     * @exception IOException if an I/O error has occurred.
     *
     */
    public int read(byte[] buf, int off, int len) throws IOException {
        ensureOpen();
        if (eos) {
            return -1;
        }
        int n = super.read(buf, off, len);
        if (n == -1) {
            if (GZIP_USE_KAE ? readTrailerKAE() : readTrailer())
                eos = true;
            else
                return this.read(buf, off, len);
        } else {
            crc.update(buf, off, n);
        }
        if (GZIP_USE_KAE && inf.finished()) {
            if (readTrailerKAE()) eos = true;
        }
        return n;
    }

    /**
     * Closes this input stream and releases any system resources associated
     * with the stream.
     * @exception IOException if an I/O error has occurred
     */
    public void close() throws IOException {
        if (!closed) {
            super.close();
            eos = true;
            closed = true;
        }
    }

    /**
     * GZIP header magic number.
     */
    public final static int GZIP_MAGIC = 0x8b1f;

    /*
     * File header flags.
     */
    private final static int FTEXT      = 1;    // Extra text
    private final static int FHCRC      = 2;    // Header CRC
    private final static int FEXTRA     = 4;    // Extra field
    private final static int FNAME      = 8;    // File name
    private final static int FCOMMENT   = 16;   // File comment

    /*
     * Reads GZIP member header and returns the total byte number
     * of this member header.
     */
    private int readHeader(InputStream this_in) throws IOException {
        CheckedInputStream in = new CheckedInputStream(this_in, crc);
        crc.reset();
        // Check header magic
        if (readUShort(in) != GZIP_MAGIC) {
            throw new ZipException("Not in GZIP format");
        }
        // Check compression method
        if (readUByte(in) != 8) {
            throw new ZipException("Unsupported compression method");
        }
        // Read flags
        int flg = readUByte(in);
        // Skip MTIME, XFL, and OS fields
        skipBytes(in, 6);
        int n = 2 + 2 + 6;
        // Skip optional extra field
        if ((flg & FEXTRA) == FEXTRA) {
            int m = readUShort(in);
            skipBytes(in, m);
            n += m + 2;
        }
        // Skip optional file name
        if ((flg & FNAME) == FNAME) {
            do {
                n++;
            } while (readUByte(in) != 0);
        }
        // Skip optional file comment
        if ((flg & FCOMMENT) == FCOMMENT) {
            do {
                n++;
            } while (readUByte(in) != 0);
        }
        // Check optional header CRC
        if ((flg & FHCRC) == FHCRC) {
            int v = (int)crc.getValue() & 0xffff;
            if (readUShort(in) != v) {
                throw new ZipException("Corrupt GZIP header");
            }
            n += 2;
        }
        crc.reset();
        return n;
    }

    /*
     * Reads GZIP member trailer and returns true if the eos
     * reached, false if there are more (concatenated gzip
     * data set)
     */
    private boolean readTrailer() throws IOException {
        if (GZIP_USE_KAE) return true;
        InputStream in = this.in;
        int n = inf.getRemaining();
        if (n > 0) {
            in = new SequenceInputStream(
                        new ByteArrayInputStream(buf, len - n, n),
                        new FilterInputStream(in) {
                            public void close() throws IOException {}
                        });
        }
        // Uses left-to-right evaluation order
        if ((readUInt(in) != crc.getValue()) ||
            // rfc1952; ISIZE is the input size modulo 2^32
            (readUInt(in) != (inf.getBytesWritten() & 0xffffffffL)))
            throw new ZipException("Corrupt GZIP trailer");

        // try concatenated case
        int m = 8;                  // this.trailer
        try {
            m += readHeader(in);    // next.header
        } catch (IOException ze) {
            return true;  // ignore any malformed, do nothing
        }
        inf.reset();
        if (n > m)
            inf.setInput(buf, len - n + m, n - m);
        return false;
    }

    /*
     * Reads GZIP member trailer and returns true if the eos
     * reached, false if there are more (concatenated gzip
     * data set)
     *
     * This method is mainly used to support the KAE-zip feature.
     */
    private boolean readTrailerKAE() throws IOException {
        InputStream in = this.in;
        int n = inf.getRemaining();
        if (n > 0) {
            in = new SequenceInputStream(
                        new ByteArrayInputStream(buf, len - n, n),
                        new FilterInputStream(in) {
                            public void close() throws IOException {}
                        });
        }
        // If there are more bytes available in "in" or the leftover in the "inf" is > 18 bytes:
        // next.header.min(10) + next.trailer(8), try concatenated case

        if (n > 18) {
            inf.reset();
            inf.setInput(buf, len - n, n);
        } else {
            try {
                fillKAE(n);
            } catch (IOException e) {
                return true;
            }
        }
        return false;
    }

    /*
     * Reads unsigned integer in Intel byte order.
     */
    private long readUInt(InputStream in) throws IOException {
        long s = readUShort(in);
        return ((long)readUShort(in) << 16) | s;
    }

    /*
     * Reads unsigned short in Intel byte order.
     */
    private int readUShort(InputStream in) throws IOException {
        int b = readUByte(in);
        return (readUByte(in) << 8) | b;
    }

    /*
     * Reads unsigned byte.
     */
    private int readUByte(InputStream in) throws IOException {
        int b = in.read();
        if (b == -1) {
            throw new EOFException();
        }
        if (b < -1 || b > 255) {
            // Report on this.in, not argument in; see read{Header, Trailer}.
            throw new IOException(this.in.getClass().getName()
                + ".read() returned value out of range -1..255: " + b);
        }
        return b;
    }

    private byte[] tmpbuf = new byte[128];

    /*
     * Skips bytes of input data blocking until all bytes are skipped.
     * Does not assume that the input stream is capable of seeking.
     */
    private void skipBytes(InputStream in, int n) throws IOException {
        while (n > 0) {
            int len = in.read(tmpbuf, 0, n < tmpbuf.length ? n : tmpbuf.length);
            if (len == -1) {
                throw new EOFException();
            }
            n -= len;
        }
    }
}
