// runme.java

import java.math.BigInteger;
import com.sifive.trace.ObjFile;
import com.sifive.trace.TraceDqr;
import com.sifive.trace.Trace;
import com.sifive.trace.Instruction;
import com.sifive.trace.NexusMessage;
import com.sifive.trace.Source;
import com.sifive.trace.TraceDecoder;
import com.sifive.trace.SWIGTYPE_p_int;
import com.sifive.trace.SWIGTYPE_p_bool;
import com.sifive.trace.SWIGTYPE_p_double;

public class pcjdqr {
  static {
    System.loadLibrary("dqr");
  }

  public static void main(String argv[]) {
    System.out.printf("%d elements in argv[]\n",argv.length);
    if (argv.length < 2) {
	    System.out.println("Usage: java pcjdqr <elf-file-name> <addr> <addr> <addr> ...");
	    System.exit(1);
    }

    System.out.printf("dqrdll version: %s\n",Trace.version());
 
    ObjFile of = new ObjFile(argv[0]);
    if (of == null) {
      System.out.println("of is null. Could not create ObjFile object");
      System.exit(1);
    }

    if (of.getStatus() != TraceDqr.DQErr.DQERR_OK) {
      System.out.println("getSatus() is not OK\n");
      System.exit(1);
    }

    Instruction instInfo = new Instruction();
    Source srcInfo = new Source();

    TraceDqr.DQErr ec = TraceDqr.DQErr.DQERR_OK;

    int i;

    i = 1;
//    unsigned long addr;
    BigInteger bigaddr;

    while ((i < argv.length) && (ec == TraceDqr.DQErr.DQERR_OK)) {
//      addr = Long.decode(argv[i]);
      bigaddr = new BigInteger(argv[i],16);
      ec = of.sourceInfo(bigaddr,instInfo,srcInfo);

      if (ec != TraceDqr.DQErr.DQERR_OK) {
          System.out.printf("Error: ObjFile.sourceInfo() failed\n");
          System.exit(1);
      }

      String sourceFile = srcInfo.sourceFileToString();
      String sourceLine = srcInfo.sourceLineToString();
      String sourceFunc = srcInfo.sourceFunctionToString();
      int sourceLineNum = (int)srcInfo.getSourceLineNum();

      if (sourceFile != null && sourceFile.length() != 0) {
          System.out.printf("File: %s:%d\n",sourceFile,sourceLineNum);
      }

      if (sourceFunc != null && sourceFunc.length() != 0) {
          System.out.printf("Function: %s\n",sourceFunc);
      }

      if (sourceLine != null && sourceLine.length() != 0) {
          System.out.printf("Source: %s\n",sourceLine);
      }


      long address = instInfo.getAddress().longValue();
      String addressLabel = instInfo.addressLabelToString();

      if (addressLabel != null && addressLabel.length() != 0) {
          System.out.printf("<%s",addressLabel);
          if (instInfo.getAddressLabelOffset() != 0) {
              System.out.printf("+%x",instInfo.getAddressLabelOffset());
          }
          System.out.printf(">%n");
      }

      String dst = String.format("    %s:",instInfo.addressToString(0));
      System.out.print(dst);

      int n = dst.length();

      for (int j = n; j < 20; j++) {
        System.out.print(" ");
      }

      dst = instInfo.instructionToString(1);
      System.out.printf("  %s",dst);
      System.out.printf("%n");

      i += 1;
    }

    System.out.println("End of Trace File");

    of.cleanUp();
  }
}
