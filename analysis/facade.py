import __main__
import atexit
from multiprocessing import Manager, Queue
from plotting import PlottingProcess
from seren.analysis.SimBuffer import SimBuffer, BufferException
import commandsource as Commands
from scipy import interpolate
import signal
import numpy
from time import sleep

#figure out if we are in interactive mode
try:
    __main__.__file__
    interactive=False
except AttributeError:
    interactive=True

def handle(e):
    '''This functions takes care of printing information about an error,
    if we are in interactive mode, or re-raising it, if we are in script mode
    (so that the execution of the script can stop if nobody catches the exception)
    '''
    if interactive:
        print str(e)
    else:
        raise e

class Singletons:
    '''Container class for singletons object. They are:
        queue
            Queue for sending commands to the plotting process
        commands
            List of the commands shared with the plotting process.
            Caution: if you modify a command, you need to reassign it in the list
            to make the changes propagate to the other process
        completedqueue
            Queue used from the plotting process to signal the completion
            of a command
    '''
    queue = Queue()
    commands = Manager().list()
    completedqueue = Queue()
    
def loadsim(run_id):
    '''Given the run_id of a simulation, reads it from the disk'''
    SimBuffer.loadsim(run_id)
    return SimBuffer.get_current_sim()
    
def plot(x,y, snap="current", sim="current", overplot = False, autoscale = True, xunit="default", yunit="default"):
    '''Particle plot. Needed arguments:
    x
        Quantity on the x-axis. Must be a string.
    y
        Quantity on the x-axis. Must be a string.
        
    Optional arguments:
    snap
        Number of the snapshot to plot. Defaults to 'current'.       
    sim
        Number of the simulation to plot. Defaults to 'current'.    
    overplot
        If True, overplots on the previous existing plot rather than deleting it. Defaults to False.
    autoscale
        If True (default), the limits of the plot are set automatically using the minimum and the maximum.
    xunit
        Specify the unit to use for the plotting for the quantity on the x-axis.
    yunit
        Specify the unit to use for the plotting for the quantity on the y-axis.
    '''
    
    
    simno = get_sim_no(sim)
    command = Commands.ParticlePlotCommand(x, y, snap, simno, overplot, autoscale, xunit, yunit)
    data = command.prepareData()
    Singletons.queue.put([command, data])
    sleep(0.001)

def render(x, y, render, snap="current", sim="current", overplot=False, autoscale=True, 
           xunit="default", yunit="default", renderunit="default",
           res=256):
    '''Render plot. Needed arguments:
    x
        Quantity on the x-axis. Must be a string.
    y
        Quantity on the x-axis. Must be a string.
    render
        Quantity to be rendered. Must be a string.
        
    Optional arguments:
    snap
        Number of the snapshot to plot. Defaults to 'current'.       
    sim
        Number of the simulation to plot. Defaults to 'current'.    
    overplot
        If True, overplots on the previous existing plot rather than deleting it. Defaults to False.
    autoscale
        If True (default), the limits of the plot are set automatically using the minimum and the maximum.
    xunit
        Specify the unit to use for the plotting for the quantity on the x-axis.
    yunit
        Specify the unit to use for the plotting for the quantity on the y-axis.
    renderunit
        Specify the unit to use for the plotting for the rendered quantity.
    res
        Specify the resolution. Can be an integer number, in which case the same resolution will be used on the two axes,
        or a tuple of two integer numbers, if you want to specify different resolutions on the two axes. 
    '''
    simno = getsimno(sim)
    command = Commands.RenderPlotCommand(x, y, render, snap, simno, overplot, autoscale, xunit, 
                                         yunit, renderunit, res)
    data = command.prepareData()
    Singletons.queue.put([command, data])

def addrender(x, y, render, **kwargs):
    '''Thin wrapper around render that sets overplot to True.
    All the other arguments are the same'''
    render(x, y, render, True, **kwargs)
    
def limit (quantity, min, max=None):
    '''First, rough implementation of limits. Quantity for now
    is either x or y, indicating the axis to limit. Min can be set
    to auto, and max can be omitted; in that case, the limits will be
    recomputed automatically. 
    '''
    command = Commands.LimitCommand (quantity, min, max)
    Singletons.queue.put([command,None])

def addplot (x,y, **kwargs):
    '''Thin wrapper around plot that sets overplot to True.
    All the other arguments are the same'''
    plot(x,y, True, **kwargs)
    
def next():
    '''Advances the current snapshot of the current simulation'''
    try:
        snap(SimBuffer.get_no_next_snapshot())
    except BufferException as e:
        handle(e)
        
def previous():
    '''Decrements the current snapshot of the current simulation'''
    try:
        snap(SimBuffer.get_no_previous_snapshot())
    except BufferException as e:
        handle(e)
        
def snap(no):
    '''Jump to the given snapshot number of the current simulation'''
    try:
        SimBuffer.set_current_snapshot_number(no)
    except BufferException as e:
        handle(e)
    
    update("current")
        
def window(no = None):
    command = Commands.WindowCommand(no)
    data = None
    Singletons.queue.put([command,data])

def subfigure(nx, ny, current):
    command = Commands.SubfigureCommand(nx, ny, current)
    data = None
    Singletons.queue.put([command,data])

def newsim(paramfile):
    return SimBuffer.newsim(paramfile)

def newsimfromparams(paramfile):
    return SimBuffer.newsimfromparams(paramfile)

def run(no=None):
    #gets the correct simulation object from the buffer
    try:
        if no is None:
            sim = SimBuffer.get_current_sim()
        else:
            sim = SimBuffer.get_sim_no(no)
    except BufferError as e:
        handle(e)
    #TODO: treat this as an exception
    if not sim.setup:
        print "The selected simulation was not set-up, we will do it for you"
        sim.SetupSimulation()
    
    sim.Run()
    
    SimBuffer.load_live_snapshot(sim)
    
    update("live")

def block():
    print "Press enter to quit..."
    raw_input()

def update(type=None):
    #updates the plots
    for command in Singletons.commands:
        updateplot=False
        if type is None:
            updateplot=True
        else:
            if command.snap == type:
                updateplot=True
        if updateplot:
            data = command.prepareData()
            Singletons.queue.put([command, data])

def savefig(name):
    command = Commands.SaveFigCommand(name)
    data = None
    Singletons.queue.put([command,data])
    time.sleep(1e-3)

def switch_nongui():
    command = Commands.SwitchNonGui()
    data = None
    Singletons.queue.put([command,data])
    time.sleep(1e-3)

def plotanalytical(x=None, y=None, overplot = True, sim = "current", snap = "current", autoscale = True):
    '''Plots the analytical solution'''
        
    #TODO: figure out automatically the quantities to plot depending on current window    
    
    simno = get_sim_no(sim)
    command = Commands.AnalyticalPlotCommand(x, y, snap, simno, overplot, autoscale)
    data = command.prepareData()
    Singletons.queue.put([command, data])

def rescale(quantity, unitname, window="current", subfig="current"):
    '''Rescales the specified quantity in the specified window to the specified unit'''
    command = Commands.RescaleCommand(quantity, unitname, window)
    Singletons.queue.put([command,None])
    okflag = Singletons.completedqueue.get()
    update()

def L1errornorm(x=None, y=None, xmin=None, xmax=None, sim = "current", snap = "current"):
    '''Computes the L1 error norm from the simulation data relative to the analytical solution'''
    
    #get the simulation number from the buffer
    simno = get_sim_no(snap)
    
    #istantiate and setup the command object to retrieve analytical solution
    command1 = Commands.AnalyticalPlotCommand(x, y, snap, simno)
    adata = command1.prepareData()

    #istantiate and setup the 2nd command object to retrieve particle data
    command2 = Commands.ParticlePlotCommand(x, y, snap, simno)
    pdata = command2.prepareData()

    #cut arrays if limits are provided
    if xmin != None and xmax != None:
        aindex = numpy.logical_and( adata.x_data > xmin, adata.x_data < xmax)
        adata.x_data = adata.x_data[aindex]
        adata.y_data = adata.y_data[aindex]
        pindex = numpy.logical_and( pdata.x_data > adata.x_data.min(),
                                    pdata.x_data < adata.x_data.max() )
        pdata.x_data = pdata.x_data[pindex]
        pdata.y_data = pdata.y_data[pindex]

    
        
    #prepare interpolation function from analytical data
    #f = interpolate.interp1d(adata.x_data[::-1], adata.y_data[::-1], kind = 'linear', axis=0, bounds_error = False)
    f = interpolate.interp1d(adata.x_data[::], adata.y_data[::], kind = 'linear', axis=0, bounds_error = False)

    #compute error norm of particle data relative to analytical data
    L1 = numpy.linalg.norm((pdata.y_data - f(pdata.x_data)), ord=1)/pdata.x_data.size
    return L1

def get_sim_no(sim):
    if sim == "current":
        simno = SimBuffer.get_current_sim_no()
    else:
        simno = sim
    return simno


def init():
    global plottingprocess
    plottingprocess = PlottingProcess(Singletons.queue, Singletons.commands, Singletons.completedqueue)
    plottingprocess.start()
    
init()

def sigint(signum, frame):
    cleanup()
    
def cleanup():
    Singletons.queue.put(["STOP",None])
    print "Waiting for background processes to finish..."
    plottingprocess.join()
    import sys
    sys.exit()
 
signal.signal(signal.SIGINT, sigint)
signal.signal(signal.SIGTERM, sigint)
signal.signal(signal.SIGSEGV, sigint)
atexit.register(cleanup)

if __name__=="__main__":
    loadsim('TEST')
    plot("x","rho")
    plotanalytical("x","rho")
    limit('x', -10.0, 10.0)
    snap(1)
    import time; time.sleep(2)
    next(); time.sleep(2)
    snap(8)
    limit('x', 'auto')
    print 'L1 error norm : ',L1errornorm("x","rho",1.0,8.0)

    block()
#    
#    loadsim('TEST')
#    plot("x","y", snap=0)
#    addplot("x", "y")
#    window()
#    plot("vx", "vy")
#    plot("vx", "x")
#    window()
#    plot("x","rho")
#    window()
#    subfigure(2,2,1)
#    plot("x", "y")
#    subfigure(2,2,2)
#    plot("vx", "vy")
#    subfigure(2,2,3)
#    plot("x", "rho")
#    subfigure(2,2,4)
#    plot("rho", "h")
#    addplot("rho", "m")
#    window(3)
#    addplot("rho", "h")
#    snap(99)
#    for i in range(10):
#        time.sleep(1)
#        previous()

