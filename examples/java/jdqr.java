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

public class jdqr {
  static {
    System.loadLibrary("dqr");
  }

  public static void main(String argv[]) {
    System.out.printf("%d elements in argv[]\n",argv.length);
    if (argv.length != 2) {
	    System.out.println("Usage: java jdqr <trace-file-name> <elf-file-name>");
	    System.exit(1);
    }

    Trace t = new Trace(argv[0],true,argv[1],32,TraceDqr.AddrDisp.ADDRDISP_WIDTHAUTO.swigValue(),0);
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
        if ((TraceDecoder.intp_value(flags) & TraceDqr.TRACE_HAVE_SRCINFO) != 0) {
          String sourceFile = srcInfo.sourceFileToString(stripPath);
          String sourceLine = srcInfo.sourceLineToString();
          int sourceLineNum = (int)srcInfo.getSourceLineNum();

          if ((lastSrcFile.compareTo(sourceFile) != 0) || (lastSrcLine.compareTo(sourceLine) != 0) || (lastSrcLineNum != sourceLineNum)) {
            lastSrcFile = sourceFile;
            lastSrcLine = sourceLine;
            lastSrcLineNum = sourceLineNum;

            if (file_flag) {
              if (sourceFile != null && sourceFile.length() != 0) {
                if (firstPrint == false) {
                  System.out.printf("%n");
                }

                if (srcBits > 0) {
                  System.out.printf("[%d] File: %s:%d\n",srcInfo.getCoreId(),sourceFile,sourceLineNum);
                }
                else {
                  System.out.printf("File: %s:%d\n",sourceFile,sourceLineNum);
                }

                firstPrint = false;
              }
            }

            if (src_flag) {
              if (sourceLine != null && sourceLine.length() != 0) {
                if (srcBits > 0) {
                  System.out.printf("[%d] Source: %s\n",srcInfo.getCoreId(),sourceLine);
                }
                else {
                  System.out.printf("Source: %s\n",sourceLine);
                }
                firstPrint = false;
              }
            }
          }
        }

        if ((dasm_flag == true) && ((TraceDecoder.intp_value(flags) & TraceDqr.TRACE_HAVE_INSTINFO) != 0)) {

          long address = instInfo.getAddress().longValue();

          if (func_flag) {
            if (srcBits > 0) {
              System.out.printf("[%d] ",instInfo.getCoreId());
            }

            if (address != (lastAddress + lastInstSize / 8)) {
              String addressLabel = instInfo.addressLabelToString();
              if (addressLabel != null && addressLabel.length() != 0) {
                System.out.printf("<%s",addressLabel);
                if (instInfo.getAddressLabelOffset() != 0) {
                  System.out.printf("+%x",instInfo.getAddressLabelOffset());
                }
                System.out.printf(">%n");
              }
            }
	    
            lastAddress = address;
            lastInstSize = instInfo.getInstSize();
          }

          if (srcBits > 0) {
            System.out.printf("[%d] ", instInfo.getCoreId());
          }

          String dst = String.format("    %s:",instInfo.addressToString(0));
          System.out.print(dst);

          int n = dst.length();

          for (int i = n; i < 20; i++) {
            System.out.print(" ");
          }

          dst = instInfo.instructionToString(instLevel);
          System.out.printf("  %s",dst);

	  int brFlag = instInfo.getBrFlags();

	  if (brFlag == TraceDqr.BranchFlags.BRFLAG_taken.swigValue()) {
              System.out.print(" [t]");
	  }
	  else if (brFlag == TraceDqr.BranchFlags.BRFLAG_notTaken.swigValue()) {
              System.out.print(" [nt]");
	  }
	  else if (brFlag == TraceDqr.BranchFlags.BRFLAG_unknown.swigValue()) {
              System.out.print(" [u]");
	  }

	  int crFlag = instInfo.getCRFlag();
	  String format = "%s";

	  if (crFlag != TraceDqr.CallReturnFlag.isNone.swigValue()) {
		System.out.print(" [");

	  	if ((crFlag & TraceDqr.CallReturnFlag.isCall.swigValue()) != 0) {
          		System.out.printf(format,"call");
			format = ",%s";
		}

		if ((crFlag & TraceDqr.CallReturnFlag.isReturn.swigValue()) != 0) {
          		System.out.printf(format,"return");
			format = ",%s";
		}

		if ((crFlag & TraceDqr.CallReturnFlag.isInterrupt.swigValue()) != 0) {
			System.out.printf(format,"interrupt");
			format = ",%s";
		}

		if ((crFlag & TraceDqr.CallReturnFlag.isSwap.swigValue()) != 0) {
			System.out.printf(format,"swap");
			format = ",%s";
		}

		if ((crFlag & TraceDqr.CallReturnFlag.isException.swigValue()) != 0) {
       			System.out.printf(format,"execption");
			format = ",%s";
		}

		if ((crFlag & TraceDqr.CallReturnFlag.isExceptionReturn.swigValue()) != 0) {
       			System.out.printf(format,"exception return");
			format = ",%s";
		}

	  	System.out.print("]");
	  }

          System.out.printf("%n");

          firstPrint = false;
        }

	if ((trace_flag || itcPrint_flag) && ((TraceDecoder.intp_value(flags) & TraceDqr.TRACE_HAVE_MSGINFO) != 0)) {
          String msgStr = msgInfo.messageToString(msgLevel);
          int core = msgInfo.getCoreId();

          coreMask |= 1 << core;
          
          if (trace_flag) {
            if (!firstPrint) {
              System.out.printf("%n");
            }

            if (srcBits > 0) {
              System.out.printf("[%d] ",core);
            }

            System.out.printf("Trace: %s",msgStr);

            System.out.printf("%n");

            firstPrint = false;
          }

          if (itcPrint_flag) {
			String printStr = "";
		    SWIGTYPE_p_bool haveStr = TraceDecoder.new_boolp();

			TraceDecoder.boolp_assign(haveStr,false);
			
			SWIGTYPE_p_double starttime = TraceDecoder.new_doublep();
			SWIGTYPE_p_double endtime = TraceDecoder.new_doublep();
			
			TraceDecoder.doublep_assign(starttime,0);
			TraceDecoder.doublep_assign(endtime,0);
			
			printStr = t.getITCPrintStr(core,haveStr,starttime,endtime);
			while (TraceDecoder.boolp_value(haveStr) != false) {
				if (firstPrint == false) {
					System.out.printf("\n");
				}

				if (srcBits > 0) {
					System.out.printf("[%d] ",core);
				}

				System.out.printf("ITC Print: ");

				if((TraceDecoder.doublep_value(starttime) != 0) || (TraceDecoder.doublep_value(endtime) != 0)) {
					System.out.printf("<%f-%f> ",TraceDecoder.doublep_value(starttime),TraceDecoder.doublep_value(endtime));
				}
				
				System.out.printf("%s",printStr);
				firstPrint = false;

				printStr = t.getITCPrintStr(core,haveStr,starttime,endtime);
			}
          }
        }
      }
    }

	if (itcPrint_flag) {
		String printStr = "";
	    SWIGTYPE_p_bool haveStr = TraceDecoder.new_boolp();

		TraceDecoder.boolp_assign(haveStr,false);
		
	    SWIGTYPE_p_double starttime = TraceDecoder.new_doublep();
		SWIGTYPE_p_double endtime = TraceDecoder.new_doublep();
		
		TraceDecoder.doublep_assign(starttime,0);
		TraceDecoder.doublep_assign(endtime,0);
		
		for (int core = 0; coreMask != 0; core++) {
			if ((coreMask & 1) != 0) {
				printStr = t.flushITCPrintStr(core,haveStr,starttime,endtime);
				while (TraceDecoder.boolp_value(haveStr) != false) {
					if (firstPrint == false) {
						System.out.printf("\n");
					}

					if (srcBits > 0) {
						System.out.printf("[%d] ",core);
					}

					System.out.printf("ITC Print: ");

					if((TraceDecoder.doublep_value(starttime) != 0) || (TraceDecoder.doublep_value(endtime) != 0)) {
						System.out.printf("<%f-%f> ",TraceDecoder.doublep_value(starttime),TraceDecoder.doublep_value(endtime));
					}
					
					System.out.printf("%s",printStr);

					firstPrint = false;

					printStr = t.flushITCPrintStr(core,haveStr,starttime,endtime);
				}
			}
			coreMask >>>= 1;
		}
	}

	System.out.println("End of Trace File");

        System.out.print(t.analyticsToString(2));

	t.cleanUp();
  }
}
