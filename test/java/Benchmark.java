import io.github.fluorescencetools.tttrlib.*;
public class Benchmark {
  static long nsRead(String f){ long t=System.nanoTime(); TTTR d=new TTTR(f); d.size(); return System.nanoTime()-t; }
  public static void main(String[] a){
    int reps = 5;
    for (String f : a) {
      long readNs=Long.MAX_VALUE; long photons=0;
      for(int r=0;r<reps;r++){ long t=System.nanoTime(); TTTR d=new TTTR(f); photons=d.size(); long e=System.nanoTime()-t; if(e<readNs) readNs=e; }
      // reconstruct (best of reps) if CLSM
      long recNs=Long.MAX_VALUE; int nf=0,nl=0,np=0; long isum=0;
      for(int r=0;r<reps;r++){
        TTTR d=new TTTR(f); VectorInt32 ch=new VectorInt32(); ch.add(0);
        long t=System.nanoTime();
        CLSMImage img=new CLSMImage(d,new CLSMSettings(),null,true,ch);
        nf=img.getN_frames(); nl=img.getN_lines(); np=img.getN_pixel();
        int[] flat=new int[Math.max(1,nf*nl*np)]; img.get_intensity_into(flat);
        long e=System.nanoTime()-t; if(e<recNs) recNs=e;
        isum=0; for(int v:flat) isum+=v;
      }
      System.out.printf("%s%n  photons=%d  read=%.1f ms (%.1f Mphot/s)  reconstruct[%dx%dx%d]=%.1f ms  intensity_sum=%d%n",
        f.substring(f.lastIndexOf('/')+1), photons, readNs/1e6, photons/(readNs/1e3), nf,nl,np, recNs/1e6, isum);
    }
  }
}
