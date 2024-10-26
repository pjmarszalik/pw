package cp2023.solution;

import cp2023.base.ComponentId;
import cp2023.base.ComponentTransfer;
import cp2023.base.DeviceId;
import cp2023.base.StorageSystem;
import cp2023.exceptions.*;

import java.util.HashMap;
import java.util.LinkedList;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Semaphore;
public class StorageSystemConcurrent implements StorageSystem {
    private final ConcurrentHashMap<DeviceId, Integer> deviceTotalSlots;
    private final ConcurrentHashMap<ComponentId, DeviceId> componentPlacement;
    private final ConcurrentHashMap<ComponentId, Semaphore> componentPlacementShield;
    private final HashMap<DeviceId, Integer> deviceFreeSpaces;
    private final ConcurrentHashMap<DeviceId, Semaphore> deviceSemaphore;
    private final ConcurrentHashMap<ComponentId, Semaphore> componentInTransferShield;
    private final ConcurrentHashMap<ComponentId, Boolean> componentInTransfer;
    private final HashMap<DeviceId, LinkedList<Meeting>> waitingOutgoing;
    private final HashMap<DeviceId, LinkedList<Meeting>> waitingIncoming;
    private final HashMap<DeviceId, Boolean> visited;
    private final HashMap<DeviceId, Meeting> path;
    private final Semaphore graphShield;
    private final Semaphore waitForTheCycle;
    private int cycle;
    public StorageSystemConcurrent(Map<DeviceId, Integer> deviceTotalSlots, Map<ComponentId, DeviceId> componentPlacement){
        if(deviceTotalSlots.isEmpty())
            throw new IllegalArgumentException();
        this.deviceTotalSlots = new ConcurrentHashMap<>();
        this.componentPlacement = new ConcurrentHashMap<>();
        this.componentPlacementShield = new ConcurrentHashMap<>();
        this.deviceFreeSpaces = new HashMap<>();
        this.deviceSemaphore = new ConcurrentHashMap<>();
        this.componentInTransferShield = new ConcurrentHashMap<>();
        this.componentInTransfer = new ConcurrentHashMap<>();
        this.waitingIncoming = new HashMap<>();
        this.waitingOutgoing = new HashMap<>();
        for(DeviceId device : deviceTotalSlots.keySet()){

            if(device == null)
                throw new IllegalArgumentException();
            if(deviceTotalSlots.get(device) == null || deviceTotalSlots.get(device) <= 0)
                throw new IllegalArgumentException();

            this.deviceTotalSlots.put(device, deviceTotalSlots.get(device));
            deviceFreeSpaces.put(device, deviceTotalSlots.get(device));
            deviceSemaphore.put(device, new Semaphore(1, true));
            waitingIncoming.put(device, new LinkedList<>());
            waitingOutgoing.put(device, new LinkedList<>());
        }
        if(componentPlacement == null)
            throw new IllegalArgumentException();
        for(ComponentId component : componentPlacement.keySet()){

            if(component == null)
                throw new IllegalArgumentException();

            if(deviceTotalSlots.get(componentPlacement.get(component)) == null)
                throw new IllegalArgumentException();

            this.componentPlacement.put(component, componentPlacement.get(component));
            componentPlacementShield.put(component, new Semaphore(1, true));
            componentInTransferShield.put(component, new Semaphore(1,true));
            componentInTransfer.put(component, false);

            deviceFreeSpaces.put(componentPlacement.get(component), deviceFreeSpaces.get(componentPlacement.get(component)) - 1);
            if(deviceFreeSpaces.get(componentPlacement.get(component)) < 0)
                throw new IllegalArgumentException();
        }

        this.visited = new HashMap<>();
        this.path = new HashMap<>();
        graphShield = new Semaphore(1, true);
        waitForTheCycle = new Semaphore(0, true);
        cycle = 0;
    }
    private boolean findCycle(DeviceId current, DeviceId wanted){
        if(current.equals(wanted))
            return true;
        visited.put(current, true);
        LinkedList<Meeting> listOfEdges = waitingIncoming.get(current);
        for (Meeting waitingMeeting : listOfEdges) {
            ComponentId waitingComponent = waitingMeeting.getIncomingComponent();
            if(waitingComponent == null)
                continue;
            DeviceId nextDevice = componentPlacement.get(waitingComponent);
            if (nextDevice == null || visited.containsKey(nextDevice))
                continue;
            if (findCycle(nextDevice, wanted)){
                path.put(current, waitingMeeting);
                cycle ++;
                waitingIncoming.get(current).remove(waitingMeeting);
                return true;
            }
        }
        return false;
    }
    private synchronized boolean checkForCycles(ComponentTransfer transfer) throws InterruptedException{
        DeviceId source = transfer.getSourceDeviceId();
        DeviceId destination = transfer.getDestinationDeviceId();
        ComponentId component = transfer.getComponentId();
        for(int i = 0; i < cycle; i ++)
            waitForTheCycle.acquire();
        graphShield.acquire();
        cycle = 0;
        this.path.clear();
        this.visited.clear();
        boolean value =  findCycle(source, destination);
        if(value){
            path.put(destination, new Meeting(component, true));
            cycle ++;
        }
        else
            cycle = 0;
        return value;
    }
    private void addComponent(ComponentTransfer transfer) throws InterruptedException{
        DeviceId destination = transfer.getDestinationDeviceId();
        ComponentId component = transfer.getComponentId();

        componentInTransferShield.put(component, new Semaphore(1, true));
        componentPlacementShield.put(component, new Semaphore(1, true));
        componentInTransferShield.get(component).acquire();
        componentInTransfer.put(component, true);
        componentInTransferShield.get(component).release();

        Meeting destinationMeeting;
        if(!waitingOutgoing.get(destination).isEmpty()) {
            destinationMeeting = waitingOutgoing.get(destination).removeFirst();
            destinationMeeting.setIncomingComponent(component);
        }
        else {
            destinationMeeting = new Meeting(component, true);
            waitingIncoming.get(destination).add(destinationMeeting);
        }
        deviceSemaphore.get(destination).release();
        graphShield.release();

        destinationMeeting.P();

        transfer.prepare();

        componentPlacement.put(component, destination);

        destinationMeeting.P();

        transfer.perform();

        componentInTransferShield.get(component).acquire();
        componentInTransfer.put(component, false);
        componentInTransferShield.get(component).release();
    }
    private void moveComponent(ComponentTransfer transfer) throws InterruptedException{
        DeviceId source = transfer.getSourceDeviceId();
        DeviceId destination = transfer.getDestinationDeviceId();
        ComponentId component = transfer.getComponentId();

        if(!waitingOutgoing.get(destination).isEmpty()){
            someoneIsWaiting(transfer);
        }
        else {
            deviceSemaphore.get(destination).release();
            graphShield.release();
            if(checkForCycles(transfer)) {
                Meeting sourceMeeting = path.get(source);
                Meeting destinationMeeting = path.get(destination);
                waitForTheCycle.release();
                graphShield.release();
                sourceMeeting.V();
                transfer.prepare();

                componentPlacement.put(component, destination);

                sourceMeeting.V();

                destinationMeeting.P();
                destinationMeeting.P();

                transfer.perform();
            }
            else{
                deviceSemaphore.get(destination).acquire();
                if(!waitingOutgoing.get(destination).isEmpty()){
                    someoneIsWaiting(transfer);
                    return;
                }
                deviceSemaphore.get(destination).release();

                Meeting destinationMeeting = new Meeting(component, true);
                waitingIncoming.get(destination).add(destinationMeeting);
                graphShield.release();

                destinationMeeting.P();

                graphShield.acquire();
                Meeting sourceMeeting;
                if(path.get(destination) != null && path.get(source) != null && path.get(destination).equals(destinationMeeting) && !waitingIncoming.get(source).contains(path.get(source))) {
                    sourceMeeting = path.get(source);
                    waitForTheCycle.release();
                }
                else {
                    deviceSemaphore.get(source).acquire();
                    if(waitingIncoming.get(source).isEmpty()){
                        sourceMeeting = new Meeting(component, false);
                        waitingOutgoing.get(source).add(sourceMeeting);
                    }
                    else{
                        sourceMeeting = waitingIncoming.get(source).removeFirst();
                        sourceMeeting.setOutgoingComponent(component);
                    }
                    deviceSemaphore.get(source).release();
                }
                graphShield.release();
                sourceMeeting.V();

                transfer.prepare();

                componentPlacement.put(component, destination);

                sourceMeeting.V();
                destinationMeeting.P();

                transfer.perform();

                graphShield.acquire();
                deviceSemaphore.get(source).acquire();
                waitingOutgoing.get(source).remove(sourceMeeting);

                if(!sourceMeeting.isAnyoneHere())
                    deviceFreeSpaces.put(source, deviceFreeSpaces.get(source) + 1);
                deviceSemaphore.get(source).release();
                graphShield.release();
            }
        }
        componentInTransferShield.get(component).acquire();
        componentInTransfer.put(component, false);
        componentInTransferShield.get(component).release();
    }
    private void someoneIsWaiting(ComponentTransfer transfer) throws InterruptedException{
        DeviceId source = transfer.getSourceDeviceId();
        DeviceId destination = transfer.getDestinationDeviceId();
        ComponentId component = transfer.getComponentId();

        Meeting sourceMeeting, destinationMeeting = waitingOutgoing.get(destination).removeFirst();
        destinationMeeting.setIncomingComponent(component);
        deviceSemaphore.get(destination).release();
        graphShield.release();

        destinationMeeting.P();

        graphShield.acquire();
        deviceSemaphore.get(source).acquire();
        if(waitingIncoming.get(source).isEmpty()){
            sourceMeeting = new Meeting(component, false);
            waitingOutgoing.get(source).add(sourceMeeting);
        }
        else{
            sourceMeeting = waitingIncoming.get(source).removeFirst();
            sourceMeeting.setOutgoingComponent(component);
        }
        deviceSemaphore.get(source).release();
        graphShield.release();

        sourceMeeting.V();

        transfer.prepare();

        componentPlacement.put(component, destination);

        sourceMeeting.V();
        destinationMeeting.P();

        transfer.perform();

        graphShield.acquire();
        deviceSemaphore.get(source).acquire();
        waitingOutgoing.get(source).remove(sourceMeeting);
        if(!sourceMeeting.isAnyoneHere())
            deviceFreeSpaces.put(source, deviceFreeSpaces.get(source) + 1);
        deviceSemaphore.get(source).release();
        graphShield.release();

        componentInTransferShield.get(component).acquire();
        componentInTransfer.put(component, false);
        componentInTransferShield.get(component).release();
    }
    private void  justDeleteComponent(ComponentTransfer transfer) throws InterruptedException{
        DeviceId source = transfer.getSourceDeviceId();
        ComponentId component = transfer.getComponentId();

        graphShield.acquire();
        Meeting sourceMeeting;
        deviceSemaphore.get(source).acquire();
        if(waitingIncoming.get(source).isEmpty()){
            sourceMeeting = new Meeting(component, false);
            waitingOutgoing.get(source).add(sourceMeeting);
        }
        else{
            sourceMeeting = waitingIncoming.get(source).removeFirst();
            sourceMeeting.setOutgoingComponent(component);
        }
        deviceSemaphore.get(source).release();
        graphShield.release();

        sourceMeeting.V();    

        transfer.prepare();

        sourceMeeting.V();

        transfer.perform();

        graphShield.acquire();
        deviceSemaphore.get(source).acquire();
        waitingOutgoing.get(source).remove(sourceMeeting);
        if(!sourceMeeting.isAnyoneHere())
            deviceFreeSpaces.put(source, deviceFreeSpaces.get(source) + 1);
        deviceSemaphore.get(source).release();
        graphShield.release();

        componentPlacement.remove(component);
        componentPlacementShield.remove(component);
        componentInTransfer.remove(component);
        componentInTransferShield.remove(component);
    }
    private void justAddComponent(ComponentTransfer transfer) throws InterruptedException{
        DeviceId destination = transfer.getDestinationDeviceId();
        ComponentId component = transfer.getComponentId();

        componentInTransferShield.put(component, new Semaphore(1, true));
        componentPlacementShield.put(component, new Semaphore(1, true));
        componentInTransferShield.get(component).acquire();
        componentInTransfer.put(component, true);
        componentInTransferShield.get(component).release();

        componentPlacement.put(component, destination);

        transfer.prepare();

        transfer.perform();

        componentInTransferShield.get(component).acquire();
        componentInTransfer.put(component, false);
        componentInTransferShield.get(component).release();
    }
    private void justMoveComponent(ComponentTransfer transfer) throws InterruptedException{
        DeviceId source = transfer.getSourceDeviceId();
        DeviceId destination = transfer.getDestinationDeviceId();
        ComponentId component = transfer.getComponentId();

        graphShield.acquire();
        Meeting sourceMeeting;
        deviceSemaphore.get(source).acquire();
        if(waitingIncoming.get(source).isEmpty()){
            sourceMeeting = new Meeting(component, false);
            waitingOutgoing.get(source).add(sourceMeeting);
        }
        else{
            sourceMeeting = waitingIncoming.get(source).removeFirst();
            sourceMeeting.setOutgoingComponent(component);
        }
        deviceSemaphore.get(source).release();
        graphShield.release();

        sourceMeeting.V(); 

        transfer.prepare();

        componentPlacement.put(component, destination);

        sourceMeeting.V();

        transfer.perform();

        graphShield.acquire();
        deviceSemaphore.get(source).acquire();
        waitingOutgoing.get(source).remove(sourceMeeting);
        if(!sourceMeeting.isAnyoneHere())
            deviceFreeSpaces.put(source, deviceFreeSpaces.get(source) + 1);
        deviceSemaphore.get(source).release();
        graphShield.release();

        componentInTransferShield.get(component).acquire();
        componentInTransfer.put(component, false);
        componentInTransferShield.get(component).release();
    }
    public void execute(ComponentTransfer transfer) throws TransferException {
        DeviceId source = transfer.getSourceDeviceId();
        DeviceId destination = transfer.getDestinationDeviceId();
        ComponentId component = transfer.getComponentId();

        if(source == null && destination == null)
            throw new IllegalTransferType(component);

        if(source != null && deviceTotalSlots.get(source) == null)
            throw new DeviceDoesNotExist(source);
        if(destination != null && deviceTotalSlots.get(destination) == null)
            throw new DeviceDoesNotExist(destination);

        if(source != null){
            if(componentInTransferShield.get(component) == null)
                throw new ComponentDoesNotExist(component, source);
            try{
                componentInTransferShield.get(component).acquire();
            } catch (Exception E){
                throw new RuntimeException("panic: unexpected thread interruption");
            }

            if(componentInTransfer.get(component)){
                componentInTransferShield.get(component).release();
                throw new ComponentIsBeingOperatedOn(component);
            }
            componentInTransfer.put(component, true);
            componentInTransferShield.get(component).release();
        }

        if(source != null){
            if(componentPlacementShield.get(component) == null)
                throw new ComponentDoesNotExist(component, source);
            try{
                componentPlacementShield.get(component).acquire();
            } catch (Exception E){
                throw new RuntimeException("panic: unexpected thread interruption");
            }
            if(componentPlacement.get(component) == null || !componentPlacement.get(component).equals(source)){
                componentPlacementShield.get(component).release();
                throw new ComponentDoesNotExist(component, source);
            }
            if(destination != null && componentPlacement.get(component).equals(destination)){
                componentPlacementShield.get(component).release();
                throw new ComponentDoesNotNeedTransfer(component, destination);
            }
            componentPlacementShield.get(component).release();
        }

        if(destination != null && source == null){
            if(componentInTransferShield.get(component) != null){
                if(componentPlacement.get(component) != null){
                    throw new ComponentAlreadyExists(component, componentPlacement.get(component));
                }
                else{
                    throw new ComponentAlreadyExists(component);
                }
            }
        }

        try{
            if(destination == null)
                justDeleteComponent(transfer);
            else {
                graphShield.acquire();
                deviceSemaphore.get(destination).acquire();
                if(deviceFreeSpaces.get(destination) > 0){
                    deviceFreeSpaces.put(destination, deviceFreeSpaces.get(destination) - 1);
                    deviceSemaphore.get(destination).release();
                    graphShield.release();
                    if(source == null)
                        justAddComponent(transfer);
                    else
                        justMoveComponent(transfer);
                }
                else{
                    if(source == null)
                        addComponent(transfer);
                    else
                        moveComponent(transfer);
                }
            }
        } catch(Exception E){
            throw new RuntimeException("panic: unexpected thread interruption");
        }
        
    }
}
