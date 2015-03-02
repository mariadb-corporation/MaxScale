/**
 * The configuration and usage information data for feeback service
 */

typedef struct {
        int	feedback_enable;	/**< Enable/Disable Notification feedback */
        char	*feedback_url;		/**< URL to which the data is sent */
        char	*feedback_user_info;	/**< User info included in the feedback data sent */
        int	feedback_send_timeout;	/**< An attempt to send the data times out and fails after this many seconds */
	int	feedback_last_action;	/**< Holds the feedback last send action status */
} FEEDBACK_CONF;


extern FEEDBACK_CONF*   notification_get_config_feedback();
