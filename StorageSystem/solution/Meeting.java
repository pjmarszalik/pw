package cp2023.solution;

import cp2023.base.ComponentId;

import java.util.Objects;
import java.util.concurrent.Semaphore;

public class Meeting {
    private ComponentId incomingComponent, outgoingComponent;
    private Semaphore semaphore;
    public Meeting(ComponentId component, boolean whichOne){
        if(whichOne){
            incomingComponent = component;
            outgoingComponent = null;
        }
        else{
            outgoingComponent = component;
            incomingComponent = null;
        }
        semaphore = new Semaphore(0, true);
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;
        Meeting meeting = (Meeting) o;
        return Objects.equals(incomingComponent, meeting.incomingComponent) && Objects.equals(outgoingComponent, meeting.outgoingComponent) && Objects.equals(semaphore, meeting.semaphore);
    }

    @Override
    public int hashCode() {
        return Objects.hash(incomingComponent, outgoingComponent, semaphore);
    }

    public synchronized void  setIncomingComponent(ComponentId incomingComponent) {
        this.incomingComponent = incomingComponent;
    }

    public void setOutgoingComponent(ComponentId outgoingComponent) {
        this.outgoingComponent = outgoingComponent;
    }

    public ComponentId getIncomingComponent() {
        return incomingComponent;
    }

    public ComponentId getOutgoingComponent() {
        return outgoingComponent;
    }

    public void P() throws InterruptedException{
        semaphore.acquire();
    }
    public void V(){
        semaphore.release();
    }
    public synchronized boolean isAnyoneHere(){
        return incomingComponent != null;
    }
}
