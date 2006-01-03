package org.openamq.client.state.listener;

import org.openamq.client.framing.AMQFrame;
import org.openamq.client.framing.Channel;
import org.openamq.client.framing.Handle;
import org.openamq.client.state.BlockingFrameListener;

/**
 * @author Robert Greig (robert.j.greig@jpmorgan.com)
 */
public class HandleReplyListener implements BlockingFrameListener
{
    private volatile boolean _done = false;

    private int _handleId;

    /**
     * Used to wait until a channel reply has been received with the specified id
     * @param handleId
     */
    public HandleReplyListener(int handleId)
    {
        _handleId = handleId;
    }

    public void frameReceived(AMQFrame frame)
    {
        Handle.Reply handleReply = (Handle.Reply) frame;
        _done = (_handleId == handleReply.handleId);
    }

    public Class getInterestingFrame()
    {
        return Handle.Reply.class;
    }

    public boolean readyToContinue()
    {
        return _done;
    }
}