// runme.java

import com.sifive.trace.TraceDqr;
import com.sifive.trace.Trace;
import com.sifive.trace.Instruction;
import com.sifive.trace.NexusMessage;
import com.sifive.trace.Source;
import com.sifive.trace.TraceDecoder;
import com.sifive.trace.SWIGTYPE_p_int;
import com.sifive.trace.SWIGTYPE_p_bool;
import com.sifive.trace.SWIGTYPE_p_double;

public class swt {
  static {
    System.loadLibrary("dqr");
  }

  public static void main(String argv[]) {
    System.out.printf("%d elements in argv[]\n",argv.length);
    if (argv.length != 2) {
	    System.out.println("Usage: java jdqr <trace-file-name> <elf-file-name>");
	    System.exit(1);
    }

    System.out.printf("dqrdll version: %s\n",Trace.version());
 
    Trace t = new Trace(argv[0],argv[1],32,TraceDqr.AddrDisp.ADDRDISP_WIDTHAUTO.swigValue(),0);
    if (t == null) {
      System.out.println("t is null");
      System.exit(1);
    }

    if (t.getStatus() != TraceDqr.DQErr.DQERR_OK) {
      System.out.println("getSatus() is not OK\n");
      System.exit(1);
    }

    t.setITCPrintOptions(4096,0);

    Instruction instInfo = new Instruction();

    NexusMessage msgInfo = new NexusMessage();;

    Source srcInfo = new Source();

    TraceDqr.DQErr ec = TraceDqr.DQErr.DQERR_OK;

    SWIGTYPE_p_int flags = TraceDecoder.new_intp();
    
    boolean func_flag = true;
    boolean file_flag = true;
    boolean dasm_flag = true;
    boolean src_flag = true;
    boolean itcPrint_flag = true;
    boolean trace_flag = true;
    int srcBits = 0;
    long lastAddress = 0;
    int lastInstSize = 0;
    String lastSrcFile = new String();
    String lastSrcLine = new String();
    int lastSrcLineNum = 0;
    int instLevel = 1;
    int msgLevel = 2;
    boolean firstPrint = true;
    String stripPath = "foo";
    int coreMask = 0;

    while (ec == TraceDqr.DQErr.DQERR_OK) {
	TraceDecoder.intp_assign(flags,0);

      ec = t.NextInstruction(instInfo,msgInfo,srcInfo,flags);

      if (ec == TraceDqr.DQErr.DQERR_OK) {

        if ((TraceDecoder.intp_value(flags) & TraceDqr.TRACE_HAVE_ITCPRINTINFO) != 0) {
          String printStr = "";
          SWIGTYPE_p_bool haveStr = TraceDecoder.new_boolp();

          TraceDecoder.boolp_assign(haveStr,false);
			
          SWIGTYPE_p_double starttime = TraceDecoder.new_doublep();
          SWIGTYPE_p_double endtime = TraceDecoder.new_doublep();
			
          TraceDecoder.doublep_assign(starttime,0);
          TraceDecoder.doublep_assign(endtime,0);
			
          printStr = t.getITCPrintStr(0,haveStr,starttime,endtime);

          while (TraceDecoder.boolp_value(haveStr) != false) {
            if (firstPrint == false) {
              System.out.printf("\n");
            }

            if (srcBits > 0) {
              System.out.printf("[%d] ",0);
            }

            System.out.printf("ITC Print: ");

            if((TraceDecoder.doublep_value(starttime) != 0) || (TraceDecoder.doublep_value(endtime) != 0)) {
              System.out.printf("<%f-%f> ",TraceDecoder.doublep_value(starttime),TraceDecoder.doublep_value(endtime));
            }
				
            System.out.printf("%s",printStr);
            firstPrint = false;

            printStr = t.getITCPrintStr(0,haveStr,starttime,endtime);
          }
        }
      }
    }

    System.out.println("End of Trace File");

    System.out.print(t.analyticsToString(2));

    t.cleanUp();
  }
}
